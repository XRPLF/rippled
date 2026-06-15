#include <xrpld/rpc/handlers/server_info/ServerDefinitions.h>

#include <xrpld/rpc/Context.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/json_writer.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

namespace xrpl {

namespace detail {

class ServerDefinitions
{
private:
    static std::string
    // translate e.g. STI_LEDGERENTRY to LedgerEntry
    translate(std::string const& inp);

    uint256 defsHash_;
    json::Value defs_;

public:
    ServerDefinitions();

    [[nodiscard]] bool
    hashMatches(uint256 hash) const
    {
        return defsHash_ == hash;
    }

    [[nodiscard]] json::Value const&
    get() const
    {
        return defs_;
    }
};

std::string
ServerDefinitions::translate(std::string const& inp)
{
    auto replace = [&](std::string_view oldStr, std::string_view newStr) -> std::string {
        std::string out = inp;
        boost::replace_all(out, oldStr, newStr);
        return out;
    };

    // TODO: use string::contains with C++23
    auto contains = [&](std::string_view s) -> bool { return inp.contains(s); };

    if (contains("UINT"))
    {
        if (contains("512") || contains("384") || contains("256") || contains("192") ||
            contains("160") || contains("128"))
        {
            return replace("UINT", "Hash");
        }

        return replace("UINT", "UInt");
    }

    static std::unordered_map<std::string_view, std::string_view> const kReplacements{
        {"OBJECT", "STObject"},
        {"ARRAY", "STArray"},
        {"ACCOUNT", "AccountID"},
        {"LEDGERENTRY", "LedgerEntry"},
        {"NOTPRESENT", "NotPresent"},
        {"PATHSET", "PathSet"},
        {"VL", "Blob"},
        {"XCHAIN_BRIDGE", "XChainBridge"},
    };

    if (auto const& it = kReplacements.find(inp); it != kReplacements.end())
    {
        return std::string(it->second);
    }

    std::string out;
    size_t pos = 0;
    std::string inpToProcess = inp;

    // convert snake_case to CamelCase
    for (;;)
    {
        pos = inpToProcess.find('_');
        if (pos == std::string::npos)
            pos = inpToProcess.size();
        std::string token = inpToProcess.substr(0, pos);
        if (token.size() > 1)
        {
            boost::algorithm::to_lower(token);
            token.data()[0] -= ('a' - 'A');
            out += token;
        }
        else
        {
            out += token;
        }
        if (pos == inpToProcess.size())
            break;
        inpToProcess = inpToProcess.substr(pos + 1);
    }
    return out;
};

ServerDefinitions::ServerDefinitions() : defs_{json::ValueType::Object}
{
    // populate SerializedTypeID names and values
    defs_[jss::TYPES] = json::ValueType::Object;

    defs_[jss::TYPES]["Done"] = -1;
    std::map<int32_t, std::string> typeMap{{-1, "Done"}};
    for (auto const& [rawName, typeValue] : kSTypeMap)
    {
        std::string const typeName = translate(std::string(rawName).substr(4) /* remove STI_ */);
        defs_[jss::TYPES][typeName] = typeValue;
        typeMap[typeValue] = typeName;
    }

    // populate LedgerEntryType names and values
    defs_[jss::LEDGER_ENTRY_TYPES] = json::ValueType::Object;
    defs_[jss::LEDGER_ENTRY_TYPES][jss::Invalid] = -1;

    for (auto const& f : LedgerFormats::getInstance())
    {
        defs_[jss::LEDGER_ENTRY_TYPES][f.getName()] = f.getType();
    }

    // populate SField serialization data
    defs_[jss::FIELDS] = json::ValueType::Array;

    uint32_t i = 0;

    {
        json::Value a = json::ValueType::Array;
        a[0U] = "Invalid";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = -1;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Unknown";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        json::Value a = json::ValueType::Array;
        a[0U] = "ObjectEndMarker";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = 1;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = true;
        v[jss::isSigningField] = true;
        v[jss::type] = "STObject";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        json::Value a = json::ValueType::Array;
        a[0U] = "ArrayEndMarker";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = 1;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = true;
        v[jss::isSigningField] = true;
        v[jss::type] = "STArray";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        json::Value a = json::ValueType::Array;
        a[0U] = "taker_gets_funded";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = 258;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Amount";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        json::Value a = json::ValueType::Array;
        a[0U] = "taker_pays_funded";
        json::Value v = json::ValueType::Object;
        v[jss::nth] = 259;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Amount";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    // copy into a sorted map to ensure deterministic output order (sorted by fieldCode)
    static std::map<int, SField const*> const kSortedFields(
        xrpl::SField::getKnownCodeToField().begin(), xrpl::SField::getKnownCodeToField().end());

    for (auto const& [code, field] : kSortedFields)
    {
        if (field->fieldName.empty())
            continue;

        json::Value innerObj = json::ValueType::Object;

        int32_t const type = field->fieldType;

        innerObj[jss::nth] = field->fieldValue;

        // whether the field is variable-length encoded this means that the length is included
        // before the content
        innerObj[jss::isVLEncoded] =
            (type == STI_VL || type == STI_ACCOUNT || type == STI_VECTOR256);
        static_assert(
            STI_VL == 7U && STI_ACCOUNT == 8U && STI_VECTOR256 == 19U,
            "STI_VL, STI_ACCOUNT, STI_VECTOR256 must be 7, 8, 19 respectively");

        // whether the field is included in serialization
        innerObj[jss::isSerialized] =
            (type < 10000 && field->fieldName != "hash" &&
             field->fieldName !=
                 "index");  // hash, index, TRANSACTION, LEDGER_ENTRY, VALIDATION, METADATA

        // whether the field is included in serialization when signing
        innerObj[jss::isSigningField] = field->shouldInclude(false);

        innerObj[jss::type] = typeMap[type];

        json::Value innerArray = json::ValueType::Array;
        innerArray[0U] = field->fieldName;
        innerArray[1U] = innerObj;

        defs_[jss::FIELDS][i++] = innerArray;
    }

    // populate TER code names and values
    defs_[jss::TRANSACTION_RESULTS] = json::ValueType::Object;

    for (auto const& [code, terInfo] : transResults())
    {
        defs_[jss::TRANSACTION_RESULTS][terInfo.first] = code;
    }

    // populate TxType names and values
    defs_[jss::TRANSACTION_TYPES] = json::ValueType::Object;
    defs_[jss::TRANSACTION_TYPES][jss::Invalid] = -1;
    for (auto const& f : TxFormats::getInstance())
    {
        defs_[jss::TRANSACTION_TYPES][f.getName()] = f.getType();
    }

    // populate TxFormats
    defs_[jss::TRANSACTION_FORMATS] = json::ValueType::Object;

    defs_[jss::TRANSACTION_FORMATS][jss::common] = json::ValueType::Array;
    auto txCommonFields = std::set<std::string>();
    for (auto const& element : TxFormats::getCommonFields())
    {
        json::Value elementObj = json::ValueType::Object;
        elementObj[jss::name] = element.sField().getName();
        elementObj[jss::optionality] = element.style();
        defs_[jss::TRANSACTION_FORMATS][jss::common].append(elementObj);
        txCommonFields.insert(element.sField().getName());
    }

    for (auto const& format : TxFormats::getInstance())
    {
        auto const& soTemplate = format.getSOTemplate();
        json::Value templateArray = json::ValueType::Array;
        for (auto const& element : soTemplate)
        {
            if (txCommonFields.contains(element.sField().getName()))
                continue;  // skip common fields, already added
            json::Value elementObj = json::ValueType::Object;
            elementObj[jss::name] = element.sField().getName();
            elementObj[jss::optionality] = element.style();
            templateArray.append(elementObj);
        }
        defs_[jss::TRANSACTION_FORMATS][format.getName()] = templateArray;
    }

    // populate LedgerFormats
    defs_[jss::LEDGER_ENTRY_FORMATS] = json::ValueType::Object;
    defs_[jss::LEDGER_ENTRY_FORMATS][jss::common] = json::ValueType::Array;
    auto ledgerCommonFields = std::set<std::string>();
    for (auto const& element : LedgerFormats::getCommonFields())
    {
        json::Value elementObj = json::ValueType::Object;
        elementObj[jss::name] = element.sField().getName();
        elementObj[jss::optionality] = element.style();
        defs_[jss::LEDGER_ENTRY_FORMATS][jss::common].append(elementObj);
        ledgerCommonFields.insert(element.sField().getName());
    }
    for (auto const& format : LedgerFormats::getInstance())
    {
        auto const& soTemplate = format.getSOTemplate();
        json::Value templateArray = json::ValueType::Array;
        for (auto const& element : soTemplate)
        {
            if (ledgerCommonFields.contains(element.sField().getName()))
                continue;  // skip common fields, already added
            json::Value elementObj = json::ValueType::Object;
            elementObj[jss::name] = element.sField().getName();
            elementObj[jss::optionality] = element.style();
            templateArray.append(elementObj);
        }
        defs_[jss::LEDGER_ENTRY_FORMATS][format.getName()] = templateArray;
    }

    defs_[jss::TRANSACTION_FLAGS] = json::ValueType::Object;
    for (auto const& [name, value] : getAllTxFlags())
    {
        json::Value txObj = json::ValueType::Object;
        for (auto const& [flagName, flagValue] : value)
        {
            txObj[flagName] = flagValue;
        }
        defs_[jss::TRANSACTION_FLAGS][name] = txObj;
    }

    defs_[jss::LEDGER_ENTRY_FLAGS] = json::ValueType::Object;
    for (auto const& [name, value] : getAllLedgerFlags())
    {
        json::Value ledgerObj = json::ValueType::Object;
        for (auto const& [flagName, flagValue] : value)
        {
            ledgerObj[flagName] = flagValue;
        }
        defs_[jss::LEDGER_ENTRY_FLAGS][name] = ledgerObj;
    }

    defs_[jss::ACCOUNT_SET_FLAGS] = json::ValueType::Object;
    for (auto const& [name, value] : getAsfFlagMap())
    {
        defs_[jss::ACCOUNT_SET_FLAGS][name] = value;
    }

    // generate hash
    {
        std::string const out = json::FastWriter().write(defs_);
        defsHash_ = xrpl::sha512Half(xrpl::Slice{out.data(), out.size()});
        defs_[jss::hash] = to_string(defsHash_);
    }
}

ServerDefinitions const&
getDefinitions()
{
    static ServerDefinitions const kDefs{};
    return kDefs;
}

}  // namespace detail

json::Value const&
getServerDefinitionsJson()
{
    return detail::getDefinitions().get();
}

json::Value
doServerDefinitions(RPC::JsonContext& context)
{
    auto& params = context.params;

    uint256 hash;
    if (params.isMember(jss::hash))
    {
        if (!params[jss::hash].isString() || !hash.parseHex(params[jss::hash].asString()))
            return RPC::invalidFieldError(jss::hash);
    }

    auto const& defs = detail::getDefinitions();
    if (defs.hashMatches(hash))
    {
        json::Value jv = json::ValueType::Object;
        jv[jss::hash] = to_string(hash);
        return jv;
    }
    return defs.get();
}

}  // namespace xrpl
