#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace xrpl {

json::Value
doAccountCurrencies(RPC::JsonContext& context)
{
    auto& params = context.params;

    if (!(params.isMember(jss::account) || params.isMember(jss::ident)))
        return RPC::missingFieldError(jss::account);

    std::string strIdent;
    if (params.isMember(jss::account))
    {
        if (!params[jss::account].isString())
            return RPC::invalidFieldError(jss::account);
        strIdent = params[jss::account].asString();
    }
    else if (params.isMember(jss::ident))
    {
        if (!params[jss::ident].isString())
            return RPC::invalidFieldError(jss::ident);
        strIdent = params[jss::ident].asString();
    }

    bool expanded = false;
    if (params.isMember(jss::expanded))
    {
        if (!params[jss::expanded].isBool())
            return RPC::invalidFieldError(jss::expanded);
        expanded = params[jss::expanded].asBool();
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
        RPC::injectError(RpcActMalformed, result);
        return result;
    }
    auto const accountID{id.value()};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(RpcActNotFound);

    auto const lines = RPCTrustLine::getItems(accountID, *ledger);

    if (!expanded)
    {
        std::set<Currency> send, receive;
        for (auto const& rspEntry : lines)
        {
            STAmount const& saBalance = rspEntry.getBalance();

            if (saBalance < rspEntry.getLimit())
                receive.insert(saBalance.get<Issue>().currency);
            if ((-saBalance) < rspEntry.getLimitPeer())
                send.insert(saBalance.get<Issue>().currency);
        }

        send.erase(badCurrency());
        receive.erase(badCurrency());

        json::Value& sendCurrencies = (result[jss::send_currencies] = json::ValueType::Array);
        for (auto const& c : send)
            sendCurrencies.append(to_string(c));

        json::Value& recvCurrencies = (result[jss::receive_currencies] = json::ValueType::Array);
        for (auto const& c : receive)
            recvCurrencies.append(to_string(c));

        return result;
    }

    // Expanded mode: report entries keyed by the asset (currency, issuer)
    // rather than bare currency codes.
    //
    // Trust lines where the requested account holds (or may hold) the
    // peer's tokens produce one entry per line with issuer = peer and
    // value = the remaining capacity of the line.
    //
    // Trust lines where the requested account is itself the issuer are
    // aggregated into a single entry per currency with issuer = the
    // requested account and no value, so that issuing accounts with many
    // holders do not produce one entry per trust line (compare the
    // aggregated "obligations" section of gateway_balances). Together the
    // two kinds of entries cover exactly the currency codes reported by
    // the legacy response format.
    //
    // Like the legacy format, membership and value are derived from the
    // limits and balances alone and do not take freeze or authorization
    // state into account; account_lines reports the full per-line state.
    std::map<std::pair<Currency, AccountID>, std::optional<STAmount>> send, receive;
    for (auto const& line : lines)
    {
        STAmount const& saBalance = line.getBalance();
        Currency const& currency = saBalance.get<Issue>().currency;
        if (currency == badCurrency())
            continue;

        auto const peerKey = std::make_pair(currency, line.getAccountIDPeer());
        auto const selfKey = std::make_pair(currency, accountID);

        // room to hold more of the peer's tokens; a negative balance is
        // owed back separately, so cap the capacity at the limit
        if (line.getLimit() > beast::kZero && saBalance < line.getLimit())
            receive.emplace(
                peerKey, saBalance > beast::kZero ? line.getLimit() - saBalance : line.getLimit());
        // holds the peer's tokens, so they can be sent
        if (saBalance > beast::kZero)
            send.emplace(peerKey, saBalance);
        // owes tokens on this line, so own issuance can be received back
        if (saBalance < beast::kZero)
            receive.emplace(selfKey, std::nullopt);
        // the peer extends trust that is not exhausted, so more can be issued
        if (line.getLimitPeer() > beast::kZero && (-saBalance) < line.getLimitPeer())
            send.emplace(selfKey, std::nullopt);
    }

    auto const appendEntries = [](json::Value& array, auto const& entries) {
        for (auto const& [key, value] : entries)
        {
            json::Value& entry = array.append(json::ValueType::Object);
            entry[jss::currency] = to_string(key.first);
            entry[jss::issuer] = to_string(key.second);
            if (value)
                entry[jss::value] = value->getText();
        }
    };

    json::Value& sendCurrencies = (result[jss::send_currencies] = json::ValueType::Array);
    appendEntries(sendCurrencies, send);

    json::Value& recvCurrencies = (result[jss::receive_currencies] = json::ValueType::Array);
    appendEntries(recvCurrencies, receive);

    return result;
}

}  // namespace xrpl
