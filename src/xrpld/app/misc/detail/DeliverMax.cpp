#include <xrpld/app/misc/DeliverMax.h>

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::rpc {

namespace {

bool
isTxnType(json::Value const& txJson, json::StaticString const& name)
{
    return txJson.isObject() && txJson[jss::TransactionType].isString() &&
        txJson[jss::TransactionType].asString() == name;
}

bool
isPayment(json::Value const& txJson)
{
    return isTxnType(txJson, jss::Payment);
}

// Calls f on each inner transaction object of a Batch.
template <class F>
void
forEachInnerTxn(json::Value& txJson, F&& f)
{
    if (!txJson.isMember(jss::RawTransactions) || !txJson[jss::RawTransactions].isArray())
        return;

    for (auto& raw : txJson[jss::RawTransactions])
    {
        if (raw.isMember(jss::RawTransaction) && raw[jss::RawTransaction].isObject())
            f(raw[jss::RawTransaction]);
    }
}

void
copyAmountToDeliverMax(json::Value& txJson, unsigned int apiVersion)
{
    if (!txJson.isMember(jss::Amount))
        return;

    txJson[jss::DeliverMax] = txJson[jss::Amount];
    if (apiVersion > 1)
        txJson.removeMember(jss::Amount);
}

json::Value
moveDeliverMaxToAmount(json::Value& txJson)
{
    if (!txJson.isMember(jss::DeliverMax))
        return {};

    if (txJson.isMember(jss::Amount))
    {
        if (txJson[jss::DeliverMax] != txJson[jss::Amount])
        {
            return makeError(
                RpcInvalidParams, "Cannot specify differing 'Amount' and 'DeliverMax'");
        }
    }
    else
    {
        txJson[jss::Amount] = txJson[jss::DeliverMax];
    }

    txJson.removeMember(jss::DeliverMax);
    return {};
}

}  // namespace

void
insertDeliverMax(json::Value& txJson, TxType txnType, unsigned int apiVersion)
{
    if (txnType == ttPAYMENT)
    {
        copyAmountToDeliverMax(txJson, apiVersion);
    }
    else if (txnType == ttBATCH)
    {
        forEachInnerTxn(txJson, [apiVersion](json::Value& inner) {
            if (isPayment(inner))
                copyAmountToDeliverMax(inner, apiVersion);
        });
    }
}

json::Value
removeDeliverMax(json::Value& txJson)
{
    if (isPayment(txJson))
        return moveDeliverMaxToAmount(txJson);

    json::Value error;
    if (!isTxnType(txJson, jss::Batch))
        return error;

    forEachInnerTxn(txJson, [&error](json::Value& inner) {
        if (error.isNull() && isPayment(inner))
            error = moveDeliverMaxToAmount(inner);
    });
    return error;
}

}  // namespace xrpl::rpc
