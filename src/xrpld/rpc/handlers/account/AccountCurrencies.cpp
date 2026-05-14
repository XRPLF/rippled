#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/TrustLine.h>

#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <set>
#include <string>

namespace xrpl {

/** Handle the `account_currencies` RPC command.
 *
 *  Enumerates the distinct currencies an account can currently send and
 *  receive, based on its trust line state in the requested ledger. The
 *  response contains two flat JSON arrays: `send_currencies` and
 *  `receive_currencies`. This is a lightweight alternative to
 *  `account_lines` when callers only need the set of currencies in play,
 *  not full trust line detail.
 *
 *  A currency appears in `receive_currencies` when the account's current
 *  balance on that trust line is below its own trust limit — i.e. there is
 *  still capacity to receive more. A currency appears in `send_currencies`
 *  when the negated balance is below the peer's trust limit — i.e. the peer
 *  can still accept more of the account's IOU, regardless of whether the
 *  account currently holds a positive balance. Freeze flags on a trust line
 *  do not affect which currencies appear in either set.
 *
 *  Validation is applied in strict order before any ledger access:
 *  (1) `account` or its legacy alias `ident` must be present and a string;
 *  (2) `RPC::lookupLedger` resolves the target ledger snapshot;
 *  (3) the identifier must be valid Base58 (returns `rpcACT_MALFORMED` if not);
 *  (4) the account must exist in the ledger (returns `rpcACT_NOT_FOUND` if not).
 *
 *  @param context The JSON RPC context, including request params and
 *      app-level services.
 *  @return A `Json::Value` object containing `send_currencies` and
 *      `receive_currencies` arrays on success, or an appropriate RPC error
 *      object on failure.
 *  @note `ident` is a legacy alias for `account`; `account` takes priority
 *      when both are present. The `badCurrency()` sentinel is defensively
 *      removed from both result sets even though well-formed ledger state
 *      should never contain it.
 *  @see RPCTrustLine::getItems, RPC::lookupLedger
 */
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

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    auto id = parseBase58<AccountID>(strIdent);
    if (!id)
    {
        RPC::injectError(RpcActMalformed, result);
        return result;
    }
    auto const accountID{id.value()};

    if (!ledger->exists(keylet::account(accountID)))
        return rpcError(RpcActNotFound);

    std::set<Currency> send, receive;
    for (auto const& rspEntry : RPCTrustLine::getItems(accountID, *ledger))
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

}  // namespace xrpl
