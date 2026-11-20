#include <xrpld/app/misc/DeliverMax.h>

#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

#include <expected>

namespace xrpl::rpc {

namespace {

// Whether TransactionType reads as `type` through txTypeFromJson.
[[nodiscard]] bool
isTxnType(json::Value const& txJson, TxType type)
{
    if (!txJson.isObject())
        return false;

    auto const read = txTypeFromJson(txJson[jss::TransactionType]);
    return read && *read == type;
}

// Calls f on each inner transaction object of a Batch, whichever field wraps
// it, until f returns false.
template <class F>
void
forEachInnerTxn(json::Value& txJson, F&& f)
{
    if (!txJson.isMember(jss::RawTransactions) || !txJson[jss::RawTransactions].isArray())
        return;

    for (auto& raw : txJson[jss::RawTransactions])
    {
        if (!raw.isObject())
            continue;

        for (auto& wrapped : raw)
        {
            if (wrapped.isObject() && !f(wrapped))
                return;
        }
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

[[nodiscard]] std::expected<void, json::Value>
replaceDeliverMaxInPayment(json::Value& txJson)
{
    if (!txJson.isMember(jss::DeliverMax))
        return {};

    if (txJson.isMember(jss::Amount))
    {
        if (txJson[jss::DeliverMax] != txJson[jss::Amount])
        {
            return std::unexpected(
                makeError(RpcInvalidParams, "Cannot specify differing 'Amount' and 'DeliverMax'"));
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

bool
isPayment(json::Value const& txJson)
{
    return isTxnType(txJson, ttPAYMENT);
}

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
            return true;
        });
    }
}

std::expected<void, json::Value>
replaceDeliverMaxWithAmount(json::Value& txJson)
{
    if (isPayment(txJson))
        return replaceDeliverMaxInPayment(txJson);

    std::expected<void, json::Value> result;
    if (!isTxnType(txJson, ttBATCH))
        return result;

    forEachInnerTxn(txJson, [&result](json::Value& inner) {
        if (isPayment(inner))
            result = replaceDeliverMaxInPayment(inner);
        return result.has_value();
    });
    return result;
}

}  // namespace xrpl::rpc
