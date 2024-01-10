//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/reporting/P2pProxy.h>
#include <ripple/json/json_value.h>
#include <ripple/json/json_writer.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/LedgerFormats.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFormats.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/Role.h>

#include <boost/algorithm/string.hpp>

#include <unordered_map>

namespace ripple {

namespace detail {

class ServerDefinitions
{
private:
    std::string
    // translate e.g. STI_LEDGERENTRY to LedgerEntry
    translate(std::string const& inp);

    uint256 defsHash_;
    Json::Value defs_;

public:
    ServerDefinitions();

    bool
    hashMatches(uint256 hash) const
    {
        return defsHash_ == hash;
    }

    Json::Value const&
    get() const
    {
        return defs_;
    }
};

std::string
ServerDefinitions::translate(std::string const& inp)
{
    auto replace = [&](char const* oldStr, char const* newStr) -> std::string {
        std::string out = inp;
        boost::replace_all(out, oldStr, newStr);
        return out;
    };

    auto contains = [&](char const* s) -> bool {
        return inp.find(s) != std::string::npos;
    };

    if (contains("UINT"))
    {
        if (contains("256") || contains("160") || contains("128"))
            return replace("UINT", "Hash");
        else
            return replace("UINT", "UInt");
    }

    std::unordered_map<std::string, std::string> replacements{
        {"OBJECT", "STObject"},
        {"ARRAY", "STArray"},
        {"ACCOUNT", "AccountID"},
        {"LEDGERENTRY", "LedgerEntry"},
        {"NOTPRESENT", "NotPresent"},
        {"PATHSET", "PathSet"},
        {"VL", "Blob"},
        {"XCHAIN_BRIDGE", "XChainBridge"},
    };

    if (auto const& it = replacements.find(inp); it != replacements.end())
    {
        return it->second;
    }

    std::string out;
    size_t pos = 0;
    std::string inpToProcess = inp;

    // convert snake_case to CamelCase
    for (;;)
    {
        pos = inpToProcess.find("_");
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
            out += token;
        if (pos == inpToProcess.size())
            break;
        inpToProcess = inpToProcess.substr(pos + 1);
    }
    return out;
};

ServerDefinitions::ServerDefinitions() : defs_{Json::objectValue}
{
    // populate SerializedTypeID names and values
    defs_[jss::TYPES] = Json::objectValue;

    defs_[jss::TYPES]["Done"] = -1;
    std::map<int32_t, std::string> typeMap{{-1, "Done"}};
    for (auto const& [rawName, typeValue] : sTypeMap)
    {
        std::string typeName =
            translate(std::string(rawName).substr(4) /* remove STI_ */);
        defs_[jss::TYPES][typeName] = typeValue;
        typeMap[typeValue] = typeName;
    }

    // populate LedgerEntryType names and values
    defs_[jss::LEDGER_ENTRY_TYPES] = Json::objectValue;
    defs_[jss::LEDGER_ENTRY_TYPES][jss::Invalid] = -1;

    for (auto const& f : LedgerFormats::getInstance())
    {
        defs_[jss::LEDGER_ENTRY_TYPES][f.getName()] = f.getType();
    }

    // populate SField serialization data
    defs_[jss::FIELDS] = Json::arrayValue;

    uint32_t i = 0;
    {
        Json::Value a = Json::arrayValue;
        a[0U] = "Generic";
        Json::Value v = Json::objectValue;
        v[jss::nth] = 0;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Unknown";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        Json::Value a = Json::arrayValue;
        a[0U] = "Invalid";
        Json::Value v = Json::objectValue;
        v[jss::nth] = -1;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Unknown";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        Json::Value a = Json::arrayValue;
        a[0U] = "ObjectEndMarker";
        Json::Value v = Json::objectValue;
        v[jss::nth] = 1;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = true;
        v[jss::isSigningField] = true;
        v[jss::type] = "STObject";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        Json::Value a = Json::arrayValue;
        a[0U] = "ArrayEndMarker";
        Json::Value v = Json::objectValue;
        v[jss::nth] = 1;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = true;
        v[jss::isSigningField] = true;
        v[jss::type] = "STArray";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        Json::Value a = Json::arrayValue;
        a[0U] = "taker_gets_funded";
        Json::Value v = Json::objectValue;
        v[jss::nth] = 258;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Amount";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    {
        Json::Value a = Json::arrayValue;
        a[0U] = "taker_pays_funded";
        Json::Value v = Json::objectValue;
        v[jss::nth] = 259;
        v[jss::isVLEncoded] = false;
        v[jss::isSerialized] = false;
        v[jss::isSigningField] = false;
        v[jss::type] = "Amount";
        a[1U] = v;
        defs_[jss::FIELDS][i++] = a;
    }

    for (auto const& [code, f] : ripple::SField::getKnownCodeToField())
    {
        if (f->fieldName == "")
            continue;

        Json::Value innerObj = Json::objectValue;

        uint32_t type = f->fieldType;

        innerObj[jss::nth] = f->fieldValue;

        // whether the field is variable-length encoded
        // this means that the length is included before the content
        innerObj[jss::isVLEncoded] =
            (type == 7U /* Blob       */ || type == 8U /* AccountID  */ ||
             type == 19U /* Vector256  */);

        // whether the field is included in serialization
        innerObj[jss::isSerialized] =
            (type < 10000 && f->fieldName != "hash" &&
             f->fieldName != "index"); /* hash, index, TRANSACTION,
                                         LEDGER_ENTRY, VALIDATION, METADATA */

        // whether the field is included in serialization when signing
        innerObj[jss::isSigningField] = f->shouldInclude(false);

        innerObj[jss::type] = typeMap[type];

        Json::Value innerArray = Json::arrayValue;
        innerArray[0U] = f->fieldName;
        innerArray[1U] = innerObj;

        defs_[jss::FIELDS][i++] = innerArray;
    }

    // populate TER code names and values
    defs_[jss::TRANSACTION_RESULTS] = Json::objectValue;

    for (auto const& [code, terInfo] : transResults())
    {
        defs_[jss::TRANSACTION_RESULTS][terInfo.first] = code;
    }

    // populate TxType names and values
    defs_[jss::TRANSACTION_TYPES] = Json::objectValue;
    defs_[jss::TRANSACTION_TYPES][jss::Invalid] = -1;
    for (auto const& f : TxFormats::getInstance())
    {
        defs_[jss::TRANSACTION_TYPES][f.getName()] = f.getType();
    }

    // generate hash
    {
        const std::string out = Json::FastWriter().write(defs_);
        defsHash_ = ripple::sha512Half(ripple::Slice{out.data(), out.size()});
        defs_[jss::hash] = to_string(defsHash_);
    }
}

}  // namespace detail

Json::Value
doServerDefinitions(RPC::JsonContext& context)
{
    auto& params = context.params;

    uint256 hash;
    if (params.isMember(jss::hash))
    {
        if (!params[jss::hash].isString() ||
            !hash.parseHex(params[jss::hash].asString()))
            return RPC::invalid_field_error(jss::hash);
    }

    static const detail::ServerDefinitions defs{};
    if (defs.hashMatches(hash))
    {
        Json::Value jv = Json::objectValue;
        jv[jss::hash] = to_string(hash);
        return jv;
    }
    return defs.get();
}

Json::Value
doServerInfo(RPC::JsonContext& context)
{
    Json::Value ret(Json::objectValue);

    ret[jss::info] = context.netOps.getServerInfo(
        true,
        context.role == Role::ADMIN,
        context.params.isMember(jss::counters) &&
            context.params[jss::counters].asBool());

    if (context.app.config().reporting())
    {
        Json::Value const proxied = forwardToP2p(context);
        auto const lf = proxied[jss::result][jss::info][jss::load_factor];
        auto const vq = proxied[jss::result][jss::info][jss::validation_quorum];
        ret[jss::info][jss::validation_quorum] = vq.isNull() ? 1 : vq;
        ret[jss::info][jss::load_factor] = lf.isNull() ? 1 : lf;
    }
    return ret;
}

}  // namespace ripple
