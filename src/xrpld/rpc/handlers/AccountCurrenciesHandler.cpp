#include <xrpld/app/paths/TrustLine.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doAccountCurrencies(RPC::JsonContext& context)
{
    auto& params = context.params;

    if (!(params.isMember(jss::account) || params.isMember(jss::ident)))
        return RPC::missing_field_error(jss::account);

    std::string strIdent;
    if (params.isMember(jss::account))
    {
        if (!params[jss::account].isString())
            return RPC::invalid_field_error(jss::account);
        strIdent = params[jss::account].asString();
    }
    else if (params.isMember(jss::ident))
    {
        if (!params[jss::ident].isString())
            return RPC::invalid_field_error(jss::ident);
        strIdent = params[jss::ident].asString();
    }

    // Get the current ledger
    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    // Get info on account.
    auto id = parseBase58<AccountID>(strIdent);
    if (!id)
    {
        RPC::inject_error(rpcACT_MALFORMED, result);
        return result;
    }
    auto const accountID{std::move(id.value())};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(rpcACT_NOT_FOUND);

    std::set<Currency> send, receive;
    for (auto const& rspEntry : RPCTrustLine::getItems(accountID, *ledger))
    {
        STAmount const& saBalance = rspEntry.getBalance();

        if (saBalance < rspEntry.getLimit())
            receive.insert(saBalance.getCurrency());
        if ((-saBalance) < rspEntry.getLimitPeer())
            send.insert(saBalance.getCurrency());
    }

    send.erase(badCurrency());
    receive.erase(badCurrency());

    Json::Value& sendCurrencies =
        (result[jss::send_currencies] = Json::arrayValue);
    for (auto const& c : send)
        sendCurrencies.append(to_string(c));

    Json::Value& recvCurrencies =
        (result[jss::receive_currencies] = Json::arrayValue);
    for (auto const& c : receive)
        recvCurrencies.append(to_string(c));

    return result;
}

}  // namespace ripple
