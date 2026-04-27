#include <test/jtx/utility.h>

#include <test/jtx/Account.h>

#include <xrpld/rpc/RPCCall.h>

#include <xrpl/basics/contract.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STParsedJSON.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <utility>
#include <vector>

namespace xrpl::test::jtx {

STObject
parse(Json::Value const& jv)
{
    STParsedJSONObject p("tx_json", jv);
    if (!p.object)
        Throw<parse_error>(rpcErrorString(p.error));
    return std::move(*p.object);
}

void
sign(Json::Value& jv, Account const& account, Json::Value& sigObject)
{
    sigObject[jss::SigningPubKey] = strHex(account.pk().slice());
    Serializer ss;
    ss.add32(HashPrefix::txSign);
    parse(jv).addWithoutSigningFields(ss);
    auto const sig = xrpl::sign(account.pk(), account.sk(), ss.slice());
    sigObject[jss::TxnSignature] = strHex(Slice{sig.data(), sig.size()});
}

void
sign(Json::Value& jv, Account const& account)
{
    sign(jv, account, jv);
}

void
fill_fee(Json::Value& jv, ReadView const& view)
{
    if (jv.isMember(jss::Fee))
        return;
    jv[jss::Fee] = to_string(view.fees().base);
}

void
fill_seq(Json::Value& jv, ReadView const& view)
{
    if (jv.isMember(jss::Sequence))
        return;
    auto const account = parseBase58<AccountID>(jv[jss::Account].asString());
    if (!account)
        Throw<parse_error>("unexpected invalid Account");
    auto const ar = view.read(keylet::account(*account));
    if (!ar)
        Throw<parse_error>("unexpected missing account root");
    jv[jss::Sequence] = ar->getFieldU32(sfSequence);
}

Json::Value
cmdToJSONRPC(std::vector<std::string> const& args, beast::Journal j, unsigned int apiVersion)
{
    Json::Value jv = Json::Value(Json::objectValue);
    auto const paramsObj = rpcCmdToJson(args, jv, apiVersion, j);

    // Re-use jv to return our formatted result.
    jv.clear();

    // Allow parser to rewrite method.
    jv[jss::method] = paramsObj.isMember(jss::method) ? paramsObj[jss::method].asString() : args[0];

    // If paramsObj is not empty, put it in a [params] array.
    if (paramsObj.begin() != paramsObj.end())
    {
        auto& paramsArray = jv[jss::params] = Json::arrayValue;
        paramsArray.append(paramsObj);
    }
    if (paramsObj.isMember(jss::jsonrpc))
        jv[jss::jsonrpc] = paramsObj[jss::jsonrpc];
    if (paramsObj.isMember(jss::ripplerpc))
        jv[jss::ripplerpc] = paramsObj[jss::ripplerpc];
    if (paramsObj.isMember(jss::id))
        jv[jss::id] = paramsObj[jss::id];
    return jv;
}

}  // namespace xrpl::test::jtx
