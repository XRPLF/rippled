#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/attester.h>
#include <test/jtx/credentials.h>
#include <test/jtx/delegate.h>
#include <test/jtx/deposit.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ticket.h>
#include <test/jtx/token.h>
#include <test/jtx/txflags.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/core/StartUpType.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xrpl::test {

enum class FieldType {
    AccountField,
    BlobField,
    ArrayField,
    CurrencyField,
    HashField,
    HashOrObjectField,
    FixedHashField,
    AssetField,
    ObjectField,
    StringField,
    TwoAccountArrayField,
    UInt32Field,
    UInt64Field,
};

std::vector<std::pair<json::StaticString, FieldType>> gMappings{
    {jss::account, FieldType::AccountField},
    {jss::accounts, FieldType::TwoAccountArrayField},
    {jss::asset, FieldType::AssetField},
    {jss::asset2, FieldType::AssetField},
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
getFieldType(json::StaticString fieldName)
{
    auto it = std::ranges::find_if(
        gMappings, [&fieldName](auto const& pair) { return pair.first == fieldName; });
    if (it != gMappings.end())
    {
        return it->second;
    }

    Throw<std::runtime_error>("`mappings` is missing field " + std::string(fieldName.cStr()));
}

std::string
getTypeName(FieldType typeID)
{
    switch (typeID)
    {
        case FieldType::AccountField:
            return "AccountID";
        case FieldType::ArrayField:
            return "array";
        case FieldType::BlobField:
            return "hex string";
        case FieldType::CurrencyField:
            return "Currency";
        case FieldType::HashField:
        case FieldType::FixedHashField:
            return "hex string";
        case FieldType::HashOrObjectField:
            return "hex string or object";
        case FieldType::AssetField:
            return "Asset";
        case FieldType::TwoAccountArrayField:
            return "length-2 array of Accounts";
        case FieldType::UInt32Field:
            return "number";
        case FieldType::UInt64Field:
            return "number";
        default:
            Throw<std::runtime_error>(
                "unknown type " + std::to_string(static_cast<uint8_t>(typeID)));
    }
}

class LedgerEntry_test : public beast::unit_test::Suite
{
    void
    checkErrorValue(
        json::Value const& jv,
        std::string const& err,
        std::string const& msg,
        std::source_location const location = std::source_location::current())
    {
        if (BEAST_EXPECT(jv.isMember(jss::status)))
            BEAST_EXPECTS(jv[jss::status] == "error", std::to_string(location.line()));
        if (BEAST_EXPECT(jv.isMember(jss::error)))
        {
            BEAST_EXPECTS(
                jv[jss::error] == err,
                "Expected error " + err + ", received " + jv[jss::error].asString() + ", at line " +
                    std::to_string(location.line()) + ", " + jv.toStyledString());
        }
        if (msg.empty())
        {
            BEAST_EXPECTS(
                jv[jss::error_message] == json::ValueType::Null || jv[jss::error_message] == "",
                "Expected no error message, received \"" + jv[jss::error_message].asString() +
                    "\", at line " + std::to_string(location.line()) + ", " + jv.toStyledString());
        }
        else if (BEAST_EXPECT(jv.isMember(jss::error_message)))
        {
            BEAST_EXPECTS(
                jv[jss::error_message] == msg,
                "Expected error message \"" + msg + "\", received \"" +
                    jv[jss::error_message].asString() + "\", at line " +
                    std::to_string(location.line()) + ", " + jv.toStyledString());
        }
    }

    static std::vector<json::Value>
    getBadValues(FieldType fieldType)
    {
        static json::Value const kInjectObject = []() {
            json::Value obj(json::ValueType::Object);
            obj[jss::account] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            obj[jss::ledger_index] = "validated";
            return obj;
        }();
        static json::Value const kInjectArray = []() {
            json::Value arr(json::ValueType::Array);
            arr[0u] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            arr[1u] = "validated";
            return arr;
        }();
        static std::array<json::Value, 21> const kAllBadValues = {
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
            "6D",                     // 16
            json::ValueType::Array,   // 17
            json::ValueType::Object,  // 18
            kInjectObject,            // 19
            kInjectArray              // 20
        };

        auto remove = [&](std::vector<std::uint8_t> indices) -> std::vector<json::Value> {
            std::unordered_set<std::uint8_t> const indexSet(indices.begin(), indices.end());
            std::vector<json::Value> values;
            values.reserve(kAllBadValues.size() - indexSet.size());
            for (std::size_t i = 0; i < kAllBadValues.size(); ++i)
            {
                if (!indexSet.contains(i))
                {
                    values.push_back(kAllBadValues[i]);
                }
            }
            return values;
        };

        static auto const& kBadAccountValues = remove({12});
        static auto const& kBadArrayValues = remove({17, 20});
        static auto const& kBadBlobValues = remove({3, 7, 8, 16});
        static auto const& kBadCurrencyValues = remove({14});
        static auto const& kBadHashValues = remove({2, 3, 7, 8, 16});
        static auto const& kBadFixedHashValues = remove({1, 2, 3, 4, 7, 8, 16});
        static auto const& kBadIndexValues = remove({12, 16, 18, 19});
        static auto const& kBadUInt32Values = remove({2, 3});
        static auto const& kBadUInt64Values = remove({2, 3});
        static auto const& kBadIssueValues = remove({});

        switch (fieldType)
        {
            case FieldType::AccountField:
                return kBadAccountValues;
            case FieldType::ArrayField:
            case FieldType::TwoAccountArrayField:
                return kBadArrayValues;
            case FieldType::BlobField:
                return kBadBlobValues;
            case FieldType::CurrencyField:
                return kBadCurrencyValues;
            case FieldType::HashField:
                return kBadHashValues;
            case FieldType::HashOrObjectField:
                return kBadIndexValues;
            case FieldType::FixedHashField:
                return kBadFixedHashValues;
            case FieldType::AssetField:
                return kBadIssueValues;
            case FieldType::UInt32Field:
                return kBadUInt32Values;
            case FieldType::UInt64Field:
                return kBadUInt64Values;
            default:
                Throw<std::runtime_error>(
                    "unknown type " + std::to_string(static_cast<uint8_t>(fieldType)));
        }
    }

    static json::Value
    getCorrectValue(json::StaticString fieldName)
    {
        static json::Value const kTwoAccountArray = []() {
            json::Value arr(json::ValueType::Array);
            arr[0u] = "rhigTLJJyXXSRUyRCQtqi1NoAZZzZnS4KU";
            arr[1u] = "r4MrUGTdB57duTnRs6KbsRGQXgkseGb1b5";
            return arr;
        }();
        static json::Value const kIssueObject = []() {
            json::Value arr(json::ValueType::Object);
            arr[jss::currency] = "XRP";
            return arr;
        }();

        auto const typeID = getFieldType(fieldName);
        switch (typeID)
        {
            case FieldType::AccountField:
                return "r4MrUGTdB57duTnRs6KbsRGQXgkseGb1b5";
            case FieldType::ArrayField:
                return json::ValueType::Array;
            case FieldType::BlobField:
                return "ABCDEF";
            case FieldType::CurrencyField:
                return "USD";
            case FieldType::HashField:
                return "5233D68B4D44388F98559DE42903767803EFA7C1F8D01413FC16EE6"
                       "B01403D6D";
            case FieldType::AssetField:
                return kIssueObject;
            case FieldType::HashOrObjectField:
                return "5233D68B4D44388F98559DE42903767803EFA7C1F8D01413FC16EE6"
                       "B01403D6D";
            case FieldType::TwoAccountArrayField:
                return kTwoAccountArray;
            case FieldType::UInt32Field:
                return 1;
            case FieldType::UInt64Field:
                return 1;
            default:
                Throw<std::runtime_error>(
                    "unknown type " + std::to_string(static_cast<uint8_t>(typeID)));
        }
    }

    void
    testMalformedField(
        test::jtx::Env& env,
        json::Value correctRequest,
        json::StaticString const fieldName,
        FieldType const typeID,
        std::string const& expectedError,
        bool required = true,
        std::source_location const location = std::source_location::current())
    {
        forAllApiVersions([&, this](unsigned apiVersion) {
            if (required)
            {
                correctRequest.removeMember(fieldName);
                json::Value const jrr = env.rpc(
                    apiVersion, "json", "ledger_entry", to_string(correctRequest))[jss::result];
                if (apiVersion < 2u)
                {
                    checkErrorValue(jrr, "unknownOption", "", location);
                }
                else
                {
                    checkErrorValue(
                        jrr, "invalidParams", "No ledger_entry params provided.", location);
                }
            }
            auto tryField = [&](json::Value fieldValue) -> void {
                correctRequest[fieldName] = fieldValue;
                json::Value const jrr = env.rpc(
                    apiVersion, "json", "ledger_entry", to_string(correctRequest))[jss::result];
                auto const expectedErrMsg =
                    RPC::expectedFieldMessage(fieldName, getTypeName(typeID));
                checkErrorValue(jrr, expectedError, expectedErrMsg, location);
            };

            auto const& badValues = getBadValues(typeID);
            for (auto const& value : badValues)
            {
                tryField(value);
            }
            if (required)
            {
                tryField(json::ValueType::Null);
            }
        });
    }

    void
    testMalformedSubfield(
        test::jtx::Env& env,
        json::Value correctRequest,
        json::StaticString parentFieldName,
        json::StaticString fieldName,
        FieldType typeID,
        std::string const& expectedError,
        bool required = true,
        std::source_location const location = std::source_location::current())
    {
        forAllApiVersions([&, this](unsigned apiVersion) {
            if (required)
            {
                correctRequest[parentFieldName].removeMember(fieldName);
                json::Value const jrr = env.rpc(
                    apiVersion, "json", "ledger_entry", to_string(correctRequest))[jss::result];
                checkErrorValue(
                    jrr, "malformedRequest", RPC::missingFieldMessage(fieldName.cStr()), location);

                correctRequest[parentFieldName][fieldName] = json::ValueType::Null;
                json::Value const jrr2 = env.rpc(
                    apiVersion, "json", "ledger_entry", to_string(correctRequest))[jss::result];
                checkErrorValue(
                    jrr2, "malformedRequest", RPC::missingFieldMessage(fieldName.cStr()), location);
            }
            auto tryField = [&](json::Value fieldValue) -> void {
                correctRequest[parentFieldName][fieldName] = fieldValue;

                json::Value const jrr = env.rpc(
                    apiVersion, "json", "ledger_entry", to_string(correctRequest))[jss::result];
                checkErrorValue(
                    jrr,
                    expectedError,
                    RPC::expectedFieldMessage(fieldName, getTypeName(typeID)),
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
        json::StaticString const& parentField,
        std::source_location const location = std::source_location::current())
    {
        testMalformedField(
            env,
            json::Value{},
            parentField,
            FieldType::HashField,
            "malformedRequest",
            true,
            location);
    }

    struct Subfield
    {
        json::StaticString fieldName;
        std::string malformedErrorMsg;
        bool required = true;
    };

    void
    runLedgerEntryTest(
        test::jtx::Env& env,
        json::StaticString const& parentField,
        std::vector<Subfield> const& subfields,
        std::source_location const location = std::source_location::current())
    {
        testMalformedField(
            env,
            json::Value{},
            parentField,
            FieldType::HashOrObjectField,
            "malformedRequest",
            true,
            location);

        json::Value correctOutput;
        correctOutput[parentField] = json::ValueType::Object;
        for (auto const& subfield : subfields)
        {
            correctOutput[parentField][subfield.fieldName] = getCorrectValue(subfield.fieldName);
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
    testInvalid()
    {
        testcase("Invalid requests");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();
        {
            // Missing ledger_entry ledger_hash
            json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::ledger_hash] =
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AA";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "lgrNotFound", "ledgerNotFound");
        }
        {
            // Missing ledger_entry ledger_hash
            json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            auto const typeId = FieldType::HashField;

            forAllApiVersions([&, this](unsigned apiVersion) {
                auto tryField = [&](json::Value fieldValue) -> void {
                    jvParams[jss::ledger_hash] = fieldValue;
                    json::Value const jrr = env.rpc(
                        apiVersion, "json", "ledger_entry", to_string(jvParams))[jss::result];
                    checkErrorValue(
                        jrr, "invalidParams", "Invalid field 'ledger_hash', not hex string.");
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
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::index] =
                "00000000000000000000000000000000000000000000000000000000000000"
                "00";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }

        forAllApiVersions([&, this](unsigned apiVersion) {
            // "features" is not an option supported by ledger_entry.
            {
                json::Value jvParams = json::ValueType::Object;
                jvParams[jss::features] =
                    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                    "AAAAAAAAAA";
                jvParams[jss::api_version] = apiVersion;
                json::Value const jrr =
                    env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

                if (apiVersion < 2u)
                {
                    checkErrorValue(jrr, "unknownOption", "");
                }
                else
                {
                    checkErrorValue(jrr, "invalidParams", "No ledger_entry params provided.");
                }
            }
        });
    }

    void
    testAccountRoot()
    {
        testcase("AccountRoot");
        using namespace test::jtx;

        auto cfg = envconfig();
        cfg->FEES.reference_fee = 10;
        Env env{*this, std::move(cfg)};

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        std::string const ledgerHash{to_string(env.closed()->header().hash)};
        {
            // Exercise ledger_closed along the way.
            json::Value const jrr = env.rpc("ledger_closed")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger_hash] == ledgerHash);
            BEAST_EXPECT(jrr[jss::ledger_index] == 3);
        }

        std::string accountRootIndex;
        {
            // Request alice's account root.
            json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
            accountRootIndex = jrr[jss::index].asString();
        }
        {
            static constexpr char kAliceAcctRootBinary[]{
                "1100612200800000240000000425000000032D00000000559CE54C3B934E4"
                "73A995B477E92EC229F99CED5B62BF4D2ACE4DC42719103AE2F6240000002"
                "540BE4008114AE123A8556F3CF91154711376AFB0F894F832B3D"};

            // Request alice's account root, but with binary == true;
            json::Value jvParams;
            jvParams[jss::account_root] = alice.human();
            jvParams[jss::binary] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr[jss::node_binary] == kAliceAcctRootBinary);
        }
        {
            // Request alice's account root using the index.
            json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(!jrr.isMember(jss::node_binary));
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Request alice's account root by index, but with binary == false.
            json::Value jvParams;
            jvParams[jss::index] = accountRootIndex;
            jvParams[jss::binary] = 0;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
        }
        {
            // Check alias
            json::Value jvParams;
            jvParams[jss::account] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node][jss::Account] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "10000000000");
            accountRootIndex = jrr[jss::index].asString();
        }
        {
            // Check malformed cases
            json::Value const jvParams;
            testMalformedField(
                env, jvParams, jss::account_root, FieldType::AccountField, "malformedAddress");
        }
        {
            // Request an account that is not in the ledger.
            json::Value jvParams;
            jvParams[jss::account_root] = Account("bob").human();
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
    }

    void
    testAmendments()
    {
        testcase("Amendments");
        using namespace test::jtx;
        Env env{*this};

        // positive test
        {
            Keylet const keylet = keylet::amendments();

            // easier to hack an object into the ledger than generate it
            // legitimately
            {
                auto const amendments = [&](OpenView& view, beast::Journal) -> bool {
                    auto const sle = std::make_shared<SLE>(keylet);

                    // Create Amendments vector (enabled amendments)
                    std::vector<uint256> enabledAmendments;
                    enabledAmendments.push_back(
                        uint256::fromVoid(
                            "42426C4D4F1009EE67080A9B7965B44656D7"
                            "714D104A72F9B4369F97ABF044EE"));
                    enabledAmendments.push_back(
                        uint256::fromVoid(
                            "4C97EBA926031A7CF7D7B36FDE3ED66DDA54"
                            "21192D63DE53FFB46E43B9DC8373"));
                    enabledAmendments.push_back(
                        uint256::fromVoid(
                            "03BDC0099C4E14163ADA272C1B6F6FABB448"
                            "CC3E51F522F978041E4B57D9158C"));
                    enabledAmendments.push_back(
                        uint256::fromVoid(
                            "35291ADD2D79EB6991343BDA0912269C817D"
                            "0F094B02226C1C14AD2858962ED4"));
                    sle->setFieldV256(sfAmendments, STVector256(enabledAmendments));

                    // Create Majorities array
                    STArray majorities;

                    auto majority1 = STObject::makeInnerObject(sfMajority);
                    majority1.setFieldH256(
                        sfAmendment,
                        uint256::fromVoid(
                            "7BB62DC13EC72B775091E9C71BF8CF97E122"
                            "647693B50C5E87A80DFD6FCFAC50"));
                    majority1.setFieldU32(sfCloseTime, 779561310);
                    majorities.pushBack(std::move(majority1));

                    auto majority2 = STObject::makeInnerObject(sfMajority);
                    majority2.setFieldH256(
                        sfAmendment,
                        uint256::fromVoid(
                            "755C971C29971C9F20C6F080F2ED96F87884"
                            "E40AD19554A5EBECDCEC8A1F77FE"));
                    majority2.setFieldU32(sfCloseTime, 779561310);
                    majorities.pushBack(std::move(majority2));

                    sle->setFieldArray(sfMajorities, majorities);

                    view.rawInsert(sle);
                    return true;
                };
                env.app().getOpenLedger().modify(amendments);
            }

            json::Value jvParams;
            jvParams[jss::amendments] = to_string(keylet.key);
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Amendments);
        }

        // negative tests
        testMalformedField(
            env, json::Value{}, jss::amendments, FieldType::FixedHashField, "malformedRequest");
    }

    void
    testAMM()
    {
        testcase("AMM");
        using namespace test::jtx;
        Account const alice{"alice"};

        auto test = [&](auto&& getAsset) {
            Env env{*this};

            // positive test
            env.fund(XRP(10000), alice);
            env.close();
            PrettyAsset const usd = getAsset(env);
            AMM const amm(env, alice, XRP(10), usd(1000));
            env.close();

            {
                json::Value jvParams;
                jvParams[jss::amm] = to_string(amm.ammID());
                auto const result = env.rpc("json", "ledger_entry", to_string(jvParams));
                BEAST_EXPECT(
                    result.isObject() && result.isMember(jss::result) &&
                    !result[jss::result].isMember(jss::error) &&
                    result[jss::result].isMember(jss::node) &&
                    result[jss::result][jss::node].isMember(sfLedgerEntryType.jsonName) &&
                    result[jss::result][jss::node][sfLedgerEntryType.jsonName] == jss::AMM);
            }

            {
                json::Value jvParams;
                json::Value ammParams(json::ValueType::Object);
                {
                    json::Value obj(json::ValueType::Object);
                    obj[jss::currency] = "XRP";
                    ammParams[jss::asset] = obj;
                }
                {
                    json::Value const obj(json::ValueType::Object);
                    ammParams[jss::asset2] = toJson(usd.raw());
                }
                jvParams[jss::amm] = ammParams;
                auto const result = env.rpc("json", "ledger_entry", to_string(jvParams));
                BEAST_EXPECT(
                    result.isObject() && result.isMember(jss::result) &&
                    !result[jss::result].isMember(jss::error) &&
                    result[jss::result].isMember(jss::node) &&
                    result[jss::result][jss::node].isMember(sfLedgerEntryType.jsonName) &&
                    result[jss::result][jss::node][sfLedgerEntryType.jsonName] == jss::AMM);
            }

            // negative tests
            runLedgerEntryTest(
                env,
                jss::amm,
                {
                    {jss::asset, "malformedRequest"},
                    {jss::asset2, "malformedRequest"},
                });
        };
        auto getIOU = [&](Env& env) -> PrettyAsset { return alice["USD"]; };
        auto getMPT = [&](Env& env) -> PrettyAsset {
            return MPTTester({.env = env, .issuer = alice});
        };
        test(getIOU);
        test(getMPT);
    }

    void
    testCheck()
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

        std::string const ledgerHash{to_string(env.closed()->header().hash)};
        {
            // Request a check.
            json::Value jvParams;
            jvParams[jss::check] = to_string(checkId.key);
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Check);
            BEAST_EXPECT(jrr[jss::node][sfSendMax.jsonName] == "100000000");
        }
        {
            // Request an index that is not a check.  We'll use alice's
            // account root index.
            std::string accountRootIndex;
            {
                json::Value jvParams;
                jvParams[jss::account_root] = alice.human();
                json::Value const jrr =
                    env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
                accountRootIndex = jrr[jss::index].asString();
            }
            json::Value jvParams;
            jvParams[jss::check] = accountRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "unexpectedLedgerType", "Unexpected ledger type.");
        }
        {
            // Check malformed cases
            runLedgerEntryTest(env, jss::check);
        }
    }

    void
    testCredentials()
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
                !jv[jss::result].isMember(jss::error) && jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] == jss::Credential);

            std::string const credIdx = jv[jss::result][jss::index].asString();

            jv = credentials::ledgerEntry(env, credIdx);
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) && jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] == jss::Credential);
        }

        {
            // Fail, credential doesn't exist
            auto const jv = credentials::ledgerEntry(
                env,
                "48004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4");
            checkErrorValue(jv[jss::result], "entryNotFound", "Entry not found.");
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
    testDelegate()
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
        std::string const ledgerHash{to_string(env.closed()->header().hash)};
        std::string delegateIndex;
        {
            // Request by account and authorize
            json::Value jvParams;
            jvParams[jss::delegate][jss::account] = alice.human();
            jvParams[jss::delegate][jss::authorize] = bob.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Delegate);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == bob.human());
            delegateIndex = jrr[jss::node][jss::index].asString();
        }
        {
            // Request by index.
            json::Value jvParams;
            jvParams[jss::delegate] = delegateIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Delegate);
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
    testDepositPreauth()
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

        std::string const ledgerHash{to_string(env.closed()->header().hash)};
        std::string depositPreauthIndex;
        {
            // Request a depositPreauth by owner and authorized.
            json::Value jvParams;
            jvParams[jss::deposit_preauth][jss::owner] = alice.human();
            jvParams[jss::deposit_preauth][jss::authorized] = becky.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::DepositPreauth);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfAuthorize.jsonName] == becky.human());
            depositPreauthIndex = jrr[jss::node][jss::index].asString();
        }
        {
            // Request a depositPreauth by index.
            json::Value jvParams;
            jvParams[jss::deposit_preauth] = depositPreauthIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::DepositPreauth);
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
    testDepositPreauthCred()
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
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] = json::ValueType::Array;
            auto& arr(jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jvParams));

            BEAST_EXPECT(
                jrr.isObject() && jrr.isMember(jss::result) &&
                !jrr[jss::result].isMember(jss::error) && jrr[jss::result].isMember(jss::node) &&
                jrr[jss::result][jss::node].isMember(sfLedgerEntryType.jsonName) &&
                jrr[jss::result][jss::node][sfLedgerEntryType.jsonName] == jss::DepositPreauth);
        }

        {
            // Failed, invalid account
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            auto tryField = [&](json::Value fieldValue) -> void {
                json::Value arr = json::ValueType::Array;
                json::Value jo;
                jo[jss::issuer] = fieldValue;
                jo[jss::credential_type] = strHex(std::string_view(credType));
                arr.append(jo);
                jvParams[jss::deposit_preauth][jss::authorized_credentials] = arr;

                json::Value const jrr =
                    env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
                auto const expectedErrMsg = fieldValue.isNull()
                    ? RPC::missingFieldMessage(jss::issuer.cStr())
                    : RPC::expectedFieldMessage(jss::issuer, "AccountID");
                checkErrorValue(jrr, "malformedAuthorizedCredentials", expectedErrMsg);
            };

            auto const& badValues = getBadValues(FieldType::AccountField);
            for (auto const& value : badValues)
            {
                tryField(value);
            }
            tryField(json::ValueType::Null);
        }

        {
            // Failed, duplicates in credentials
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] = json::ValueType::Array;
            auto& arr(jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(jo);
            arr.append(std::move(jo));
            auto const jrr = env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                RPC::expectedFieldMessage(jss::authorized_credentials, "array"));
        }

        {
            // Failed, invalid credential_type
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();

            auto tryField = [&](json::Value fieldValue) -> void {
                json::Value arr = json::ValueType::Array;
                json::Value jo;
                jo[jss::issuer] = issuer.human();
                jo[jss::credential_type] = fieldValue;
                arr.append(jo);
                jvParams[jss::deposit_preauth][jss::authorized_credentials] = arr;

                json::Value const jrr =
                    env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
                auto const expectedErrMsg = fieldValue.isNull()
                    ? RPC::missingFieldMessage(jss::credential_type.cStr())
                    : RPC::expectedFieldMessage(jss::credential_type, "hex string");
                checkErrorValue(jrr, "malformedAuthorizedCredentials", expectedErrMsg);
            };

            auto const& badValues = getBadValues(FieldType::BlobField);
            for (auto const& value : badValues)
            {
                tryField(value);
            }
            tryField(json::ValueType::Null);
        }

        {
            // Failed, authorized and authorized_credentials both present
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized] = alice.human();

            jvParams[jss::deposit_preauth][jss::authorized_credentials] = json::ValueType::Array;
            auto& arr(jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            json::Value jo;
            jo[jss::issuer] = issuer.human();
            jo[jss::credential_type] = strHex(std::string_view(credType));
            arr.append(std::move(jo));

            auto const jrr = env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedRequest",
                "Must have exactly one of `authorized` and "
                "`authorized_credentials`.");
        }

        {
            // Failed, authorized_credentials is not an array
            json::Value jvParams;
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
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] = json::ValueType::Array;
            auto& arr(jvParams[jss::deposit_preauth][jss::authorized_credentials]);
            arr.append("foobar");

            auto const jrr = env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                "Invalid field 'authorized_credentials', not array of objects.");
        }

        {
            // Failed, authorized_credentials contains arrays
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] = json::ValueType::Array;
            auto& arr(jvParams[jss::deposit_preauth][jss::authorized_credentials]);
            json::Value payload = json::ValueType::Array;
            payload.append(42);
            arr.append(std::move(payload));

            auto const jrr = env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                "Invalid field 'authorized_credentials', not array of objects.");
        }

        {
            // Failed, authorized_credentials is empty array
            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] = json::ValueType::Array;

            auto const jrr = env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                "Invalid field 'authorized_credentials', array empty.");
        }

        {
            // Failed, authorized_credentials is too long
            static std::array<std::string_view, 9> const kCredTypes = {
                "cred1", "cred2", "cred3", "cred4", "cred5", "cred6", "cred7", "cred8", "cred9"};
            static_assert(sizeof(kCredTypes) / sizeof(kCredTypes[0]) > kMaxCredentialsArraySize);

            json::Value jvParams;
            jvParams[jss::ledger_index] = jss::validated;
            jvParams[jss::deposit_preauth][jss::owner] = bob.human();
            jvParams[jss::deposit_preauth][jss::authorized_credentials] = json::ValueType::Array;

            auto& arr(jvParams[jss::deposit_preauth][jss::authorized_credentials]);

            for (auto cred : kCredTypes)
            {
                json::Value jo;
                jo[jss::issuer] = issuer.human();
                jo[jss::credential_type] = strHex(std::string_view(cred));
                arr.append(std::move(jo));
            }

            auto const jrr = env.rpc("json", "ledger_entry", to_string(jvParams));
            checkErrorValue(
                jrr[jss::result],
                "malformedAuthorizedCredentials",
                "Invalid field 'authorized_credentials', array too long.");
        }
    }

    void
    testDirectory()
    {
        testcase("Directory");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env.trust(usd(1000), alice);
        env.close();

        // Run up the number of directory entries so alice has two
        // directory nodes.
        for (int d = 1'000'032; d >= 1'000'000; --d)
        {
            env(offer(alice, usd(1), drops(d)));
        }
        env.close();

        std::string const ledgerHash{to_string(env.closed()->header().hash)};
        {
            // Exercise ledger_closed along the way.
            json::Value const jrr = env.rpc("ledger_closed")[jss::result];
            BEAST_EXPECT(jrr[jss::ledger_hash] == ledgerHash);
            BEAST_EXPECT(jrr[jss::ledger_index] == 5);
        }

        std::string const dirRootIndex =
            "A33EC6BB85FB5674074C4A3A43373BB17645308F3EAE1933E3E35252162B217D";
        {
            // Locate directory by index.
            json::Value jvParams;
            jvParams[jss::directory] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 32);
        }
        {
            // Locate directory by directory root.
            json::Value jvParams;
            jvParams[jss::directory] = json::ValueType::Object;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by owner.
            json::Value jvParams;
            jvParams[jss::directory] = json::ValueType::Object;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == dirRootIndex);
        }
        {
            // Locate directory by directory root and sub_index.
            json::Value jvParams;
            jvParams[jss::directory] = json::ValueType::Object;
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::directory][jss::sub_index] = 1;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] != dirRootIndex);
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 2);
        }
        {
            // Locate directory by owner and sub_index.
            json::Value jvParams;
            jvParams[jss::directory] = json::ValueType::Object;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::directory][jss::sub_index] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::index] != dirRootIndex);
            BEAST_EXPECT(jrr[jss::node][sfIndexes.jsonName].size() == 2);
        }
        {
            // Bad directory argument.
            json::Value jvParams;
            jvParams[jss::ledger_hash] = ledgerHash;
            testMalformedField(
                env, jvParams, jss::directory, FieldType::HashOrObjectField, "malformedRequest");
        }
        {
            // Non-integer sub_index.
            json::Value jvParams;
            jvParams[jss::directory] = json::ValueType::Object;
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
            json::Value jvParams;
            jvParams[jss::directory] = json::ValueType::Object;

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
            json::Value jvParams;
            jvParams[jss::directory] = json::ValueType::Object;
            jvParams[jss::directory][jss::owner] = alice.human();
            jvParams[jss::directory][jss::dir_root] = dirRootIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(
                jrr, "malformedRequest", "Must have exactly one of `owner` and `dir_root` fields.");
        }
        {
            // Incomplete directory object.  Missing both dir_root and owner.
            json::Value jvParams;
            jvParams[jss::directory] = json::ValueType::Object;
            jvParams[jss::directory][jss::sub_index] = 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(
                jrr, "malformedRequest", "Must have exactly one of `owner` and `dir_root` fields.");
        }
    }

    void
    testEscrow()
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
            json::Value jv;
            jv[jss::TransactionType] = jss::EscrowCreate;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
            jv[sfFinishAfter.jsonName] = cancelAfter.time_since_epoch().count() + 2;
            return jv;
        };

        using namespace std::chrono_literals;
        env(escrowCreate(alice, alice, XRP(333), env.now() + 2s));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->header().hash)};
        std::string escrowIndex;
        {
            // Request the escrow using owner and sequence.
            json::Value jvParams;
            jvParams[jss::escrow] = json::ValueType::Object;
            jvParams[jss::escrow][jss::owner] = alice.human();
            jvParams[jss::escrow][jss::seq] = env.seq(alice) - 1;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::Amount] == XRP(333).value().getText());
            escrowIndex = jrr[jss::index].asString();
        }
        {
            // Request the escrow by index.
            json::Value jvParams;
            jvParams[jss::escrow] = escrowIndex;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::Amount] == XRP(333).value().getText());
        }
        {
            // Malformed escrow fields
            runLedgerEntryTest(
                env, jss::escrow, {{jss::owner, "malformedOwner"}, {jss::seq, "malformedSeq"}});
        }
    }

    void
    testFeeSettings()
    {
        testcase("Fee Settings");
        using namespace test::jtx;
        Env env{*this};

        // positive test
        {
            Keylet const keylet = keylet::fees();
            json::Value jvParams;
            jvParams[jss::fee] = to_string(keylet.key);
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::FeeSettings);
        }

        // negative tests
        testMalformedField(
            env, json::Value{}, jss::fee, FieldType::FixedHashField, "malformedRequest");
    }

    void
    testLedgerHashes()
    {
        testcase("Ledger Hashes");
        using namespace test::jtx;
        Env env{*this};

        // positive test
        {
            Keylet const keylet = keylet::skip();
            json::Value jvParams;
            jvParams[jss::hashes] = to_string(keylet.key);
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::LedgerHashes);
        }

        // negative tests
        testMalformedField(
            env, json::Value{}, jss::hashes, FieldType::FixedHashField, "malformedRequest");
    }

    void
    testNFTokenOffer()
    {
        testcase("NFT Offer");
        using namespace test::jtx;
        Env env{*this};

        // positive test
        Account const issuer{"issuer"};
        Account const buyer{"buyer"};
        env.fund(XRP(1000), issuer, buyer);

        uint256 const nftokenID0 = token::getNextID(env, issuer, 0, tfTransferable);
        env(token::mint(issuer, 0), Txflags(tfTransferable));
        env.close();
        uint256 const offerID = keylet::nftoffer(issuer, env.seq(issuer)).key;
        env(token::createOffer(issuer, nftokenID0, drops(1)),
            token::Destination(buyer),
            Txflags(tfSellNFToken));

        {
            json::Value jvParams;
            jvParams[jss::nft_offer] = to_string(offerID);
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::NFTokenOffer);
            BEAST_EXPECT(jrr[jss::node][sfOwner.jsonName] == issuer.human());
            BEAST_EXPECT(jrr[jss::node][sfNFTokenID.jsonName] == to_string(nftokenID0));
            BEAST_EXPECT(jrr[jss::node][sfAmount.jsonName] == "1");
        }

        // negative tests
        runLedgerEntryTest(env, jss::nft_offer);
    }

    void
    testNFTokenPage()
    {
        testcase("NFT Page");
        using namespace test::jtx;
        Env env{*this};

        // positive test
        Account const issuer{"issuer"};
        env.fund(XRP(1000), issuer);

        env(token::mint(issuer, 0), Txflags(tfTransferable));
        env.close();

        auto const nftpage = keylet::nftpageMax(issuer);
        BEAST_EXPECT(env.le(nftpage) != nullptr);

        {
            json::Value jvParams;
            jvParams[jss::nft_page] = to_string(nftpage.key);
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::NFTokenPage);
        }

        // negative tests
        runLedgerEntryTest(env, jss::nft_page);
    }

    void
    testNegativeUNL()
    {
        testcase("Negative UNL");
        using namespace test::jtx;
        Env env{*this};

        // positive test
        {
            Keylet const keylet = keylet::negativeUNL();

            // easier to hack an object into the ledger than generate it
            // legitimately
            {
                auto const nUNL = [&](OpenView& view, beast::Journal) -> bool {
                    auto const sle = std::make_shared<SLE>(keylet);

                    // Create DisabledValidators array
                    STArray disabledValidators;
                    auto disabledValidator = STObject::makeInnerObject(sfDisabledValidator);
                    auto pubKeyBlob = strUnHex(
                        "ED58F6770DB5DD77E59D28CB650EC3816E2FC95021BB56E720C9A1"
                        "2DA79C58A3AB");
                    disabledValidator.setFieldVL(sfPublicKey, *pubKeyBlob);
                    disabledValidator.setFieldU32(sfFirstLedgerSequence, 91371264);
                    disabledValidators.pushBack(std::move(disabledValidator));

                    sle->setFieldArray(sfDisabledValidators, disabledValidators);
                    sle->setFieldH256(
                        sfPreviousTxnID,
                        uint256::fromVoid(
                            "8D47FFE664BE6C335108DF689537625855A6"
                            "A95160CC6D351341B9"
                            "2624D9C5E3"));
                    sle->setFieldU32(sfPreviousTxnLgrSeq, 91442944);

                    view.rawInsert(sle);
                    return true;
                };
                env.app().getOpenLedger().modify(nUNL);
            }

            json::Value jvParams;
            jvParams[jss::nunl] = to_string(keylet.key);
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::NegativeUNL);
        }

        // negative tests
        testMalformedField(
            env, json::Value{}, jss::nunl, FieldType::FixedHashField, "malformedRequest");
    }

    void
    testOffer()
    {
        testcase("Offer");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env(offer(alice, usd(321), XRP(322)));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->header().hash)};
        std::string offerIndex;
        {
            // Request the offer using owner and sequence.
            json::Value jvParams;
            jvParams[jss::offer] = json::ValueType::Object;
            jvParams[jss::offer][jss::account] = alice.human();
            jvParams[jss::offer][jss::seq] = env.seq(alice) - 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
            offerIndex = jrr[jss::index].asString();
        }
        {
            // Request the offer using its index.
            json::Value jvParams;
            jvParams[jss::offer] = offerIndex;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][jss::TakerGets] == "322000000");
        }

        {
            // Malformed offer fields
            runLedgerEntryTest(
                env,
                jss::offer,
                {{jss::account, "malformedAddress"}, {jss::seq, "malformedRequest"}});
        }
    }

    void
    testPayChan()
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
            json::Value jv;
            jv[jss::TransactionType] = jss::PaymentChannelCreate;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::Values::None);
            jv[sfSettleDelay.jsonName] = settleDelay.count();
            jv[sfPublicKey.jsonName] = strHex(pk.slice());
            return jv;
        };

        env(payChanCreate(alice, env.master, XRP(57), 18s, alice.pk()));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->header().hash)};

        uint256 const payChanIndex{keylet::payChan(alice, env.master, env.seq(alice) - 1).key};
        {
            // Request the payment channel using its index.
            json::Value jvParams;
            jvParams[jss::payment_channel] = to_string(payChanIndex);
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfAmount.jsonName] == "57000000");
            BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName] == "0");
            BEAST_EXPECT(jrr[jss::node][sfSettleDelay.jsonName] == 18);
        }
        {
            // Request an index that is not a payment channel.
            json::Value jvParams;
            jvParams[jss::payment_channel] = ledgerHash;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }

        {
            // Malformed paychan field
            runLedgerEntryTest(env, jss::payment_channel);
        }
    }

    void
    testRippleState()
    {
        testcase("RippleState");
        using namespace test::jtx;
        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gateway"};
        auto const usd = gw["USD"];
        env.fund(XRP(10000), alice, gw);
        env.close();

        env.trust(usd(999), alice);
        env.close();

        env(pay(gw, alice, usd(97)));
        env.close();

        // check both aliases
        for (auto const& fieldName : {jss::ripple_state, jss::state})
        {
            std::string const ledgerHash{to_string(env.closed()->header().hash)};
            {
                // Request the trust line using the accounts and currency.
                json::Value jvParams;
                jvParams[fieldName] = json::ValueType::Object;
                jvParams[fieldName][jss::accounts] = json::ValueType::Array;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                json::Value const jrr =
                    env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
                BEAST_EXPECT(jrr[jss::node][sfBalance.jsonName][jss::value] == "-97");
                BEAST_EXPECT(jrr[jss::node][sfHighLimit.jsonName][jss::value] == "999");
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
                json::Value jvParams;
                jvParams[fieldName] = json::ValueType::Object;
                jvParams[fieldName][jss::accounts] = json::ValueType::Array;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                json::Value const jrr =
                    env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(
                    jrr,
                    "malformedRequest",
                    "Invalid field 'accounts', not length-2 array of "
                    "Accounts.");
            }
            {
                // ripple_state more than 2 accounts.
                json::Value jvParams;
                jvParams[fieldName] = json::ValueType::Object;
                jvParams[fieldName][jss::accounts] = json::ValueType::Array;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = gw.human();
                jvParams[fieldName][jss::accounts][2u] = alice.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                json::Value const jrr =
                    env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(
                    jrr,
                    "malformedRequest",
                    "Invalid field 'accounts', not length-2 array of "
                    "Accounts.");
            }
            {
                // ripple_state account[0] / account[1] is not an account.
                json::Value jvParams;
                jvParams[fieldName] = json::ValueType::Object;
                auto tryField = [&](json::Value badAccount) -> void {
                    {
                        // account[0]
                        jvParams[fieldName][jss::accounts] = json::ValueType::Array;
                        jvParams[fieldName][jss::accounts][0u] = badAccount;
                        jvParams[fieldName][jss::accounts][1u] = gw.human();
                        jvParams[fieldName][jss::currency] = "USD";

                        json::Value const jrr =
                            env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
                        checkErrorValue(
                            jrr,
                            "malformedAddress",
                            RPC::expectedFieldMessage(jss::accounts, "array of Accounts"));
                    }

                    {
                        // account[1]
                        jvParams[fieldName][jss::accounts] = json::ValueType::Array;
                        jvParams[fieldName][jss::accounts][0u] = alice.human();
                        jvParams[fieldName][jss::accounts][1u] = badAccount;
                        jvParams[fieldName][jss::currency] = "USD";

                        json::Value const jrr =
                            env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
                        checkErrorValue(
                            jrr,
                            "malformedAddress",
                            RPC::expectedFieldMessage(jss::accounts, "array of Accounts"));
                    }
                };

                auto const& badValues = getBadValues(FieldType::AccountField);
                for (auto const& value : badValues)
                {
                    tryField(value);
                }
                tryField(json::ValueType::Null);
            }
            {
                // ripple_state account[0] == account[1].
                json::Value jvParams;
                jvParams[fieldName] = json::ValueType::Object;
                jvParams[fieldName][jss::accounts] = json::ValueType::Array;
                jvParams[fieldName][jss::accounts][0u] = alice.human();
                jvParams[fieldName][jss::accounts][1u] = alice.human();
                jvParams[fieldName][jss::currency] = "USD";
                jvParams[jss::ledger_hash] = ledgerHash;
                json::Value const jrr =
                    env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
                checkErrorValue(jrr, "malformedRequest", "Cannot have a trustline to self.");
            }
        }
    }

    void
    testSignerList()
    {
        testcase("Signer List");
        using namespace test::jtx;
        Env env{*this};
        runLedgerEntryTest(env, jss::signer_list);
    }

    void
    testTicket()
    {
        testcase("Ticket");
        using namespace test::jtx;
        Env env{*this};
        env.close();

        // Create two tickets.
        std::uint32_t const tkt1{env.seq(env.master) + 1};
        env(ticket::create(env.master, 2));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->header().hash)};
        // Request four tickets: one before the first one we created, the
        // two created tickets, and the ticket that would come after the
        // last created ticket.
        {
            // Not a valid ticket requested by index.
            json::Value jvParams;
            jvParams[jss::ticket] = to_string(getTicketIndex(env.master, tkt1 - 1));
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // First real ticket requested by index.
            json::Value jvParams;
            jvParams[jss::ticket] = to_string(getTicketIndex(env.master, tkt1));
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Ticket);
            BEAST_EXPECT(jrr[jss::node][sfTicketSequence.jsonName] == tkt1);
        }
        {
            // Second real ticket requested by account and sequence.
            json::Value jvParams;
            jvParams[jss::ticket] = json::ValueType::Object;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] = tkt1 + 1;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][jss::index] == to_string(getTicketIndex(env.master, tkt1 + 1)));
        }
        {
            // Not a valid ticket requested by account and sequence.
            json::Value jvParams;
            jvParams[jss::ticket] = json::ValueType::Object;
            jvParams[jss::ticket][jss::account] = env.master.human();
            jvParams[jss::ticket][jss::ticket_seq] = tkt1 + 2;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // Request a ticket using an account root entry.
            json::Value jvParams;
            jvParams[jss::ticket] = to_string(keylet::account(env.master).key);
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "unexpectedLedgerType", "Unexpected ledger type.");
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
    testDID()
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
            json::Value jv;
            jv[jss::TransactionType] = jss::DIDSet;
            jv[jss::Account] = account.human();
            jv[sfDIDDocument.jsonName] = strHex(std::string{"data"});
            jv[sfURI.jsonName] = strHex(std::string{"uri"});
            return jv;
        };

        env(didCreate(alice));
        env.close();

        std::string const ledgerHash{to_string(env.closed()->header().hash)};

        {
            // Request the DID using its index.
            json::Value jvParams;
            jvParams[jss::did] = alice.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfDIDDocument.jsonName] == strHex(std::string{"data"}));
            BEAST_EXPECT(jrr[jss::node][sfURI.jsonName] == strHex(std::string{"uri"}));
        }
        {
            // Request an index that is not a DID.
            json::Value jvParams;
            jvParams[jss::did] = env.master.human();
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // Malformed DID index
            json::Value const jvParams;
            testMalformedField(
                env, jvParams, jss::did, FieldType::AccountField, "malformedAddress");
        }
    }

    void
    testInvalidOracleLedgerEntry()
    {
        testcase("Invalid Oracle Ledger Entry");
        using namespace xrpl::test::jtx;
        using namespace xrpl::test::jtx::oracle;

        Env env(*this);
        Account const owner("owner");
        env.fund(XRP(1'000), owner);
        Oracle const oracle(
            env, {.owner = owner, .fee = static_cast<int>(env.current()->fees().base.drops())});

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
        using namespace xrpl::test::jtx;
        using namespace xrpl::test::jtx::oracle;

        Env env(*this);
        auto const baseFee = static_cast<int>(env.current()->fees().base.drops());
        std::vector<AccountID> accounts;
        std::vector<std::uint32_t> oracles;
        for (int i = 0; i < 10; ++i)
        {
            Account const owner(std::string("owner") + std::to_string(i));
            env.fund(XRP(1'000), owner);
            // different accounts can have the same asset pair
            Oracle const oracle(env, {.owner = owner, .documentID = i, .fee = baseFee});
            accounts.push_back(owner.id());
            oracles.push_back(oracle.documentID());
            // same account can have different asset pair
            Oracle const oracle1(env, {.owner = owner, .documentID = i + 10, .fee = baseFee});
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
                return Oracle::ledgerEntry(env, accounts[i], std::to_string(oracles[i]));
            }();
            try
            {
                BEAST_EXPECT(jv[jss::node][jss::Owner] == to_string(accounts[i]));
            }
            catch (...)
            {
                fail();
            }
        }
    }

    void
    testMPT()
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
             .flags = tfMPTCanLock | tfMPTRequireAuth | tfMPTCanEscrow | tfMPTCanTrade |
                 tfMPTCanTransfer | tfMPTCanClawback});
        mptAlice.authorize({.account = bob, .holderCount = 1});

        std::string const ledgerHash{to_string(env.closed()->header().hash)};

        std::string const badMptID = "00000193B9DDCAF401B5B3B26875986043F82CD0D13B4315";
        {
            // Request the MPTIssuance using its MPTIssuanceID.
            json::Value jvParams;
            jvParams[jss::mpt_issuance] = strHex(mptAlice.issuanceID());
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::node][sfMPTokenMetadata.jsonName] == strHex(std::string{"123"}));
            BEAST_EXPECT(jrr[jss::node][jss::mpt_issuance_id] == strHex(mptAlice.issuanceID()));
        }
        {
            // Request an index that is not a MPTIssuance.
            json::Value jvParams;
            jvParams[jss::mpt_issuance] = badMptID;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // Request the MPToken using its owner + mptIssuanceID.
            json::Value jvParams;
            jvParams[jss::mptoken] = json::ValueType::Object;
            jvParams[jss::mptoken][jss::account] = bob.human();
            jvParams[jss::mptoken][jss::mpt_issuance_id] = strHex(mptAlice.issuanceID());
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            BEAST_EXPECT(
                jrr[jss::node][sfMPTokenIssuanceID.jsonName] == strHex(mptAlice.issuanceID()));
        }
        {
            // Request the MPToken using a bad mptIssuanceID.
            json::Value jvParams;
            jvParams[jss::mptoken] = json::ValueType::Object;
            jvParams[jss::mptoken][jss::account] = bob.human();
            jvParams[jss::mptoken][jss::mpt_issuance_id] = badMptID;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                env.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // Malformed MPTIssuance index
            json::Value const jvParams;
            testMalformedField(
                env, jvParams, jss::mptoken, FieldType::HashOrObjectField, "malformedRequest");
        }
    }

    void
    testPermissionedDomain()
    {
        testcase("PermissionedDomain");

        using namespace test::jtx;

        Env env(*this, testableAmendments() | featurePermissionedDomains);
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
            json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain][jss::account] = alice.human();
            params[jss::permissioned_domain][jss::seq] = seq;
            auto jv = env.rpc("json", "ledger_entry", to_string(params));
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) && jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] == jss::PermissionedDomain);

            std::string const pdIdx = jv[jss::result][jss::index].asString();
            BEAST_EXPECT(strHex(keylet::permissionedDomain(alice, seq).key) == pdIdx);

            params.clear();
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain] = pdIdx;
            jv = env.rpc("json", "ledger_entry", to_string(params));
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                !jv[jss::result].isMember(jss::error) && jv[jss::result].isMember(jss::node) &&
                jv[jss::result][jss::node].isMember(sfLedgerEntryType.jsonName) &&
                jv[jss::result][jss::node][sfLedgerEntryType.jsonName] == jss::PermissionedDomain);
        }

        {
            // Fail, invalid permissioned domain index
            json::Value params;
            params[jss::ledger_index] = jss::validated;
            params[jss::permissioned_domain] =
                "12F1F1F1F180D67377B2FAB292A31C922470326268D2B9B74CD1E582645B9A"
                "DE";
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params));
            checkErrorValue(jrr[jss::result], "entryNotFound", "Entry not found.");
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

    /// Test the ledger entry types that don't take parameters
    void
    testFixed()
    {
        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, envconfig([](auto cfg) {
                    cfg->START_UP = StartUpType::Fresh;
                    return cfg;
                })};

        env.close();

        /** Verifies that the RPC result has the expected data
         *
         * @param good: Indicates that the request should have succeeded
         *   and returned a ledger object of `expectedType` type.
         * @param jv: The RPC result Json value
         * @param expectedType: The type that the ledger object should
         *   have if "good".
         * @param expectedError: Optional. The expected error if not
         *   good. Defaults to "entryNotFound".
         */
        auto checkResult = [&](bool good,
                               json::Value const& jv,
                               json::StaticString const& expectedType,
                               std::optional<std::string> const& expectedError = {}) {
            if (good)
            {
                BEAST_EXPECTS(
                    jv.isObject() && jv.isMember(jss::result) &&
                        !jv[jss::result].isMember(jss::error) &&
                        jv[jss::result].isMember(jss::node) &&
                        jv[jss::result][jss::node].isMember(sfLedgerEntryType.jsonName) &&
                        jv[jss::result][jss::node][sfLedgerEntryType.jsonName] == expectedType,
                    to_string(jv));
            }
            else
            {
                BEAST_EXPECTS(
                    jv.isObject() && jv.isMember(jss::result) &&
                        jv[jss::result].isMember(jss::error) &&
                        !jv[jss::result].isMember(jss::node) &&
                        jv[jss::result][jss::error] == expectedError.value_or("entryNotFound"),
                    to_string(jv));
            }
        };

        /** Runs a series of tests for a given fixed-position ledger
         * entry.
         *
         * @param field: The Json request field to use.
         * @param expectedType: The type that the ledger object should
         *   have if "good".
         * @param expectedKey: The keylet of the fixed object.
         * @param good: Indicates whether the object is expected to
         *   exist.
         */
        auto test = [&](json::StaticString const& field,
                        json::StaticString const& expectedType,
                        Keylet const& expectedKey,
                        bool good) {
            testcase << expectedType.cStr() << (good ? "" : " not") << " found";

            auto const hexKey = strHex(expectedKey.key);

            {
                // Test bad values
                // "field":null
                json::Value params;
                params[jss::ledger_index] = jss::validated;
                params[field] = json::ValueType::Null;
                auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                checkResult(false, jv, expectedType, "malformedRequest");
                BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
            }

            {
                json::Value params;
                // "field":"string"
                params[jss::ledger_index] = jss::validated;
                params[field] = "arbitrary string";
                auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                checkResult(false, jv, expectedType, "malformedRequest");
                BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
            }

            {
                json::Value params;
                // "field":false
                params[jss::ledger_index] = jss::validated;
                params[field] = false;
                auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                checkResult(false, jv, expectedType, "invalidParams");
                BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
            }

            {
                json::Value params;

                // "field":[incorrect index hash]
                auto const badKey = strHex(expectedKey.key + uint256{1});
                params[jss::ledger_index] = jss::validated;
                params[field] = badKey;
                auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                checkResult(false, jv, expectedType, "entryNotFound");
                BEAST_EXPECTS(jv[jss::result][jss::index] == badKey, to_string(jv));
            }

            {
                json::Value params;
                // "index":"field" using API 2
                params[jss::ledger_index] = jss::validated;
                params[jss::index] = field;
                params[jss::api_version] = 2;
                auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                checkResult(false, jv, expectedType, "malformedRequest");
                BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
            }

            std::string const pdIdx = [&]() {
                {
                    json::Value params;
                    // Test good values
                    // Use the "field":true notation
                    params[jss::ledger_index] = jss::validated;
                    params[field] = true;
                    auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                    // Index will always be returned for valid parameters.
                    std::string const pdIdx = jv[jss::result][jss::index].asString();
                    BEAST_EXPECTS(hexKey == pdIdx, to_string(jv));
                    checkResult(good, jv, expectedType);

                    return pdIdx;
                }
            }();

            {
                json::Value params;
                // "field":"[index hash]"
                params[jss::ledger_index] = jss::validated;
                params[field] = hexKey;
                auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                checkResult(good, jv, expectedType);
                BEAST_EXPECT(jv[jss::result][jss::index].asString() == hexKey);
            }

            {
                // Bad value
                // Use the "index":"field" notation with API 2
                json::Value params;
                params[jss::ledger_index] = jss::validated;
                params[jss::index] = field;
                params[jss::api_version] = 2;
                auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                checkResult(false, jv, expectedType, "malformedRequest");
                BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
            }

            {
                json::Value params;
                // Use the "index":"field" notation with API 3
                params[jss::ledger_index] = jss::validated;
                params[jss::index] = field;
                params[jss::api_version] = 3;
                auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                // Index is correct either way
                BEAST_EXPECT(jv[jss::result][jss::index].asString() == hexKey);
                checkResult(good, jv, expectedType);
            }

            {
                json::Value params;
                // Use the "index":"[index hash]" notation
                params[jss::ledger_index] = jss::validated;
                params[jss::index] = pdIdx;
                auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                // Index is correct either way
                BEAST_EXPECT(jv[jss::result][jss::index].asString() == hexKey);
                checkResult(good, jv, expectedType);
            }
        };

        test(jss::amendments, jss::Amendments, keylet::amendments(), true);
        test(jss::fee, jss::FeeSettings, keylet::fees(), true);
        // There won't be an nunl
        test(jss::nunl, jss::NegativeUNL, keylet::negativeUNL(), false);
        // Can only get the short skip list this way
        test(jss::hashes, jss::LedgerHashes, keylet::skip(), true);
    }

    void
    testHashes()
    {
        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, envconfig([](auto cfg) {
                    cfg->START_UP = StartUpType::Fresh;
                    return cfg;
                })};

        env.close();

        /** Verifies that the RPC result has the expected data
         *
         * @param good: Indicates that the request should have succeeded
         *   and returned a ledger object of `expectedType` type.
         * @param jv: The RPC result Json value
         * @param expectedCount: The number of Hashes expected in the
         *   object if "good".
         * @param expectedError: Optional. The expected error if not
         *   good. Defaults to "entryNotFound".
         */
        auto checkResult = [&](bool good,
                               json::Value const& jv,
                               int expectedCount,
                               std::optional<std::string> const& expectedError = {}) {
            if (good)
            {
                BEAST_EXPECTS(
                    jv.isObject() && jv.isMember(jss::result) &&
                        !jv[jss::result].isMember(jss::error) &&
                        jv[jss::result].isMember(jss::node) &&
                        jv[jss::result][jss::node].isMember(sfLedgerEntryType.jsonName) &&
                        jv[jss::result][jss::node][sfLedgerEntryType.jsonName] == jss::LedgerHashes,
                    to_string(jv));
                BEAST_EXPECTS(
                    jv[jss::result].isMember(jss::node) &&
                        jv[jss::result][jss::node].isMember("Hashes") &&
                        jv[jss::result][jss::node]["Hashes"].size() == expectedCount,
                    to_string(jv[jss::result][jss::node]["Hashes"].size()));
            }
            else
            {
                BEAST_EXPECTS(
                    jv.isObject() && jv.isMember(jss::result) &&
                        jv[jss::result].isMember(jss::error) &&
                        !jv[jss::result].isMember(jss::node) &&
                        jv[jss::result][jss::error] == expectedError.value_or("entryNotFound"),
                    to_string(jv));
            }
        };

        /** Runs a series of tests for a given ledger index.
         *
         * @param ledger: The ledger index value of the "hashes" request
         *   parameter. May not necessarily be a number.
         * @param expectedKey: The expected keylet of the object.
         * @param good: Indicates whether the object is expected to
         *   exist.
         * @param expectedCount: The number of Hashes expected in the
         *   object if "good".
         */
        auto test =
            [&](json::Value ledger, Keylet const& expectedKey, bool good, int expectedCount = 0) {
                testcase << "LedgerHashes: seq: " << env.current()->header().seq
                         << " \"hashes\":" << to_string(ledger) << (good ? "" : " not") << " found";

                auto const hexKey = strHex(expectedKey.key);

                {
                    // Test bad values
                    // "hashes":null
                    json::Value params;
                    params[jss::ledger_index] = jss::validated;
                    params[jss::hashes] = json::ValueType::Null;
                    auto jv = env.rpc("json", "ledger_entry", to_string(params));
                    checkResult(false, jv, 0, "malformedRequest");
                    BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
                }

                {
                    json::Value params;
                    // "hashes":"non-uint string"
                    params[jss::ledger_index] = jss::validated;
                    params[jss::hashes] = "arbitrary string";
                    auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                    checkResult(false, jv, 0, "malformedRequest");
                    BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
                }

                {
                    json::Value params;
                    // "hashes":"uint string" is invalid, too
                    params[jss::ledger_index] = jss::validated;
                    params[jss::hashes] = "10";
                    auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                    checkResult(false, jv, 0, "malformedRequest");
                    BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
                }

                {
                    json::Value params;
                    // "hashes":false
                    params[jss::ledger_index] = jss::validated;
                    params[jss::hashes] = false;
                    auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                    checkResult(false, jv, 0, "invalidParams");
                    BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
                }

                {
                    json::Value params;
                    // "hashes":-1
                    params[jss::ledger_index] = jss::validated;
                    params[jss::hashes] = -1;
                    auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                    checkResult(false, jv, 0, "internal");
                    BEAST_EXPECT(!jv[jss::result].isMember(jss::index));
                }

                // "hashes":[incorrect index hash]
                {
                    json::Value params;
                    auto const badKey = strHex(expectedKey.key + uint256{1});
                    params[jss::ledger_index] = jss::validated;
                    params[jss::hashes] = badKey;
                    auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                    checkResult(false, jv, 0, "entryNotFound");
                    BEAST_EXPECT(jv[jss::result][jss::index] == badKey);
                }

                {
                    json::Value params;
                    // Test good values
                    // Use the "hashes":ledger notation
                    params[jss::ledger_index] = jss::validated;
                    params[jss::hashes] = ledger;
                    auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                    checkResult(good, jv, expectedCount);
                    // Index will always be returned for valid parameters.
                    std::string const pdIdx = jv[jss::result][jss::index].asString();
                    BEAST_EXPECTS(hexKey == pdIdx, strHex(pdIdx));
                }

                {
                    json::Value params;
                    // "hashes":"[index hash]"
                    params[jss::ledger_index] = jss::validated;
                    params[jss::hashes] = hexKey;
                    auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                    checkResult(good, jv, expectedCount);
                    // Index is correct either way
                    BEAST_EXPECTS(
                        hexKey == jv[jss::result][jss::index].asString(),
                        strHex(jv[jss::result][jss::index].asString()));
                }

                {
                    json::Value params;
                    // Use the "index":"[index hash]" notation
                    params[jss::ledger_index] = jss::validated;
                    params[jss::index] = hexKey;
                    auto const jv = env.rpc("json", "ledger_entry", to_string(params));
                    checkResult(good, jv, expectedCount);
                    // Index is correct either way
                    BEAST_EXPECTS(
                        hexKey == jv[jss::result][jss::index].asString(),
                        strHex(jv[jss::result][jss::index].asString()));
                }
            };

        // short skip list
        test(true, keylet::skip(), true, 2);
        // long skip list at index 0
        test(1, keylet::skip(1), false);
        // long skip list at index 1
        test(1 << 17, keylet::skip(1 << 17), false);

        // Close more ledgers, but stop short of the flag ledger
        for (auto i = env.current()->seq(); i <= 250; ++i)
            env.close();

        // short skip list
        test(true, keylet::skip(), true, 249);
        // long skip list at index 0
        test(1, keylet::skip(1), false);
        // long skip list at index 1
        test(1 << 17, keylet::skip(1 << 17), false);

        // Close a flag ledger so the first "long" skip list is created
        for (auto i = env.current()->seq(); i <= 260; ++i)
            env.close();

        // short skip list
        test(true, keylet::skip(), true, 256);
        // long skip list at index 0
        test(1, keylet::skip(1), true, 1);
        // long skip list at index 1
        test(1 << 17, keylet::skip(1 << 17), false);
    }

    void
    testCLI()
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

        // Request a check.
        json::Value const jrr = env.rpc("ledger_entry", to_string(checkId.key))[jss::result];
        BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Check);
        BEAST_EXPECT(jrr[jss::node][sfSendMax.jsonName] == "100000000");
    }

public:
    void
    run() override
    {
        testInvalid();
        testAccountRoot();
        testAmendments();
        testAMM();
        testCheck();
        testCredentials();
        testDelegate();
        testDepositPreauth();
        testDepositPreauthCred();
        testDirectory();
        testEscrow();
        testFeeSettings();
        testLedgerHashes();
        testNFTokenOffer();
        testNFTokenPage();
        testNegativeUNL();
        testOffer();
        testPayChan();
        testRippleState();
        testSignerList();
        testTicket();
        testDID();
        testInvalidOracleLedgerEntry();
        testOracleLedgerEntry();
        testMPT();
        testPermissionedDomain();
        testFixed();
        testHashes();
        testCLI();
    }
};

class LedgerEntry_XChain_test : public beast::unit_test::Suite,
                                public test::jtx::XChainBridgeObjects
{
    void
    checkErrorValue(json::Value const& jv, std::string const& err, std::string const& msg)
    {
        if (BEAST_EXPECT(jv.isMember(jss::status)))
            BEAST_EXPECT(jv[jss::status] == "error");
        if (BEAST_EXPECT(jv.isMember(jss::error)))
            BEAST_EXPECT(jv[jss::error] == err);
        if (msg.empty())
        {
            BEAST_EXPECT(
                jv[jss::error_message] == json::ValueType::Null || jv[jss::error_message] == "");
        }
        else if (BEAST_EXPECT(jv.isMember(jss::error_message)))
        {
            BEAST_EXPECT(jv[jss::error_message] == msg);
        }
    }

    void
    testBridge()
    {
        testcase("ledger_entry: bridge");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(), features);

        createBridgeObjects(mcEnv, scEnv);

        std::string const ledgerHash{to_string(mcEnv.closed()->header().hash)};
        std::string bridgeIndex;
        json::Value mcBridge;
        {
            // request the bridge via RPC
            json::Value jvParams;
            jvParams[jss::bridge_account] = mcDoor.human();
            jvParams[jss::bridge] = jvb;
            json::Value const jrr =
                mcEnv.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

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
            bridgeIndex = r[jss::index].asString();
            mcBridge = r;
        }
        {
            // request the bridge via RPC by index
            json::Value jvParams;
            jvParams[jss::index] = bridgeIndex;
            json::Value const jrr =
                mcEnv.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            BEAST_EXPECT(jrr[jss::node] == mcBridge);
        }
        {
            // swap door accounts and make sure we get an error value
            json::Value jvParams;
            // Sidechain door account is "master", not scDoor
            jvParams[jss::bridge_account] = Account::kMaster.human();
            jvParams[jss::bridge] = jvb;
            jvParams[jss::ledger_hash] = ledgerHash;
            json::Value const jrr =
                mcEnv.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
        {
            // create two claim ids and verify that the bridge counter was
            // incremented
            mcEnv(xchainCreateClaimId(mcAlice, jvb, reward, scAlice));
            mcEnv.close();
            mcEnv(xchainCreateClaimId(mcBob, jvb, reward, scBob));
            mcEnv.close();

            // request the bridge via RPC
            json::Value jvParams;
            jvParams[jss::bridge_account] = mcDoor.human();
            jvParams[jss::bridge] = jvb;
            json::Value const jrr =
                mcEnv.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            // we executed two create claim id txs
            BEAST_EXPECT(r.isMember(sfXChainClaimID.jsonName));
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 2);
        }
    }

    void
    testClaimID()
    {
        testcase("ledger_entry: xchain_claim_id");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(), features);

        createBridgeObjects(mcEnv, scEnv);

        scEnv(xchainCreateClaimId(scAlice, jvb, reward, mcAlice));
        scEnv.close();
        scEnv(xchainCreateClaimId(scBob, jvb, reward, mcBob));
        scEnv.close();

        {
            // request the xchain_claim_id via RPC
            json::Value jvParams;
            jvParams[jss::xchain_owned_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_claim_id][jss::xchain_owned_claim_id] = 1;
            json::Value const jrr =
                scEnv.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == scAlice.human());
            BEAST_EXPECT(r[sfLedgerEntryType.jsonName] == jss::XChainOwnedClaimID);
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 1);
            BEAST_EXPECT(r[sfOwnerNode.jsonName].asInt() == 0);
        }

        {
            // request the xchain_claim_id via RPC
            json::Value jvParams;
            jvParams[jss::xchain_owned_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_claim_id][jss::xchain_owned_claim_id] = 2;
            json::Value const jrr =
                scEnv.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == scBob.human());
            BEAST_EXPECT(r[sfLedgerEntryType.jsonName] == jss::XChainOwnedClaimID);
            BEAST_EXPECT(r[sfXChainClaimID.jsonName].asInt() == 2);
            BEAST_EXPECT(r[sfOwnerNode.jsonName].asInt() == 0);
        }
    }

    void
    testCreateAccountClaimID()
    {
        testcase("ledger_entry: xchain_create_account_claim_id");
        using namespace test::jtx;

        Env mcEnv{*this, features};
        Env scEnv(*this, envconfig(), features);

        // note: signers.size() and quorum are both 5 in createBridgeObjects
        createBridgeObjects(mcEnv, scEnv);

        auto scCarol = Account("scCarol");  // Don't fund it - it will be created with the
                                            // xchain transaction
        auto const amt = XRP(1000);
        mcEnv(sidechainXchainAccountCreate(mcAlice, jvb, scCarol, amt, reward));
        mcEnv.close();

        // send less than quorum of attestations (otherwise funds are
        // immediately transferred and no "claim" object is created)
        static constexpr size_t kNumAttest = 3;
        auto attestations = createAccountAttestations(
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
            kUtXchainDefaultNumSigners);
        for (size_t i = 0; i < kNumAttest; ++i)
        {
            scEnv(attestations[i]);
        }
        scEnv.close();

        {
            // request the create account claim_id via RPC
            json::Value jvParams;
            jvParams[jss::xchain_owned_create_account_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_create_account_claim_id]
                    [jss::xchain_owned_create_account_claim_id] = 1;
            json::Value const jrr =
                scEnv.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];

            BEAST_EXPECT(jrr.isMember(jss::node));
            auto r = jrr[jss::node];

            BEAST_EXPECT(r.isMember(jss::Account));
            BEAST_EXPECT(r[jss::Account] == Account::kMaster.human());

            BEAST_EXPECT(r.isMember(sfXChainAccountCreateCount.jsonName));
            BEAST_EXPECT(r[sfXChainAccountCreateCount.jsonName].asInt() == 1);

            BEAST_EXPECT(r.isMember(sfXChainCreateAccountAttestations.jsonName));
            auto attest = r[sfXChainCreateAccountAttestations.jsonName];
            BEAST_EXPECT(attest.isArray());
            BEAST_EXPECT(attest.size() == 3);
            BEAST_EXPECT(
                attest[json::Value::UInt(0)].isMember(sfXChainCreateAccountProofSig.jsonName));
            json::Value a[kNumAttest];
            for (size_t i = 0; i < kNumAttest; ++i)
            {
                a[i] = attest[json::Value::UInt(0)][sfXChainCreateAccountProofSig.jsonName];
                BEAST_EXPECT(
                    a[i].isMember(jss::Amount) && a[i][jss::Amount].asInt() == 1000 * kDropPerXrp);
                BEAST_EXPECT(
                    a[i].isMember(jss::Destination) && a[i][jss::Destination] == scCarol.human());
                BEAST_EXPECT(
                    a[i].isMember(sfAttestationSignerAccount.jsonName) &&
                    std::ranges::any_of(signers, [&](Signer const& s) {
                        return a[i][sfAttestationSignerAccount.jsonName] == s.account.human();
                    }));
                BEAST_EXPECT(
                    a[i].isMember(sfAttestationRewardAccount.jsonName) &&
                    std::ranges::any_of(payee, [&](Account const& account) {
                        return a[i][sfAttestationRewardAccount.jsonName] == account.human();
                    }));
                BEAST_EXPECT(
                    a[i].isMember(sfWasLockingChainSend.jsonName) &&
                    a[i][sfWasLockingChainSend.jsonName] == 1);
                BEAST_EXPECT(
                    a[i].isMember(sfSignatureReward.jsonName) &&
                    a[i][sfSignatureReward.jsonName].asInt() == 1 * kDropPerXrp);
            }
        }

        // complete attestations quorum - CreateAccountClaimID should not be
        // present anymore
        for (size_t i = kNumAttest; i < kUtXchainDefaultNumSigners; ++i)
        {
            scEnv(attestations[i]);
        }
        scEnv.close();
        {
            // request the create account claim_id via RPC
            json::Value jvParams;
            jvParams[jss::xchain_owned_create_account_claim_id] = jvXRPBridgeRPC;
            jvParams[jss::xchain_owned_create_account_claim_id]
                    [jss::xchain_owned_create_account_claim_id] = 1;
            json::Value const jrr =
                scEnv.rpc("json", "ledger_entry", to_string(jvParams))[jss::result];
            checkErrorValue(jrr, "entryNotFound", "Entry not found.");
        }
    }

public:
    void
    run() override
    {
        testBridge();
        testClaimID();
        testCreateAccountClaimID();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerEntry, rpc, xrpl);
BEAST_DEFINE_TESTSUITE(LedgerEntry_XChain, rpc, xrpl);

}  // namespace xrpl::test
