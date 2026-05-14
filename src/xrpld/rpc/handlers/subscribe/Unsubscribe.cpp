/** @file
 *  Implements the `unsubscribe` RPC/WebSocket command handler.
 *
 *  Tears down active push subscriptions previously registered by
 *  `doSubscribe`. The symmetric counterpart is Subscribe.cpp in the same
 *  directory. All state removal is delegated to `NetworkOPs` via
 *  `context.netOps`.
 */

#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/InfoSub.h>
#include <xrpl/server/NetworkOPs.h>

#include <string>

namespace xrpl {

/** Remove active push subscriptions for a WebSocket or HTTP-callback client.
 *
 *  Resolves the subscriber identity, then processes up to five independent
 *  subscription categories from the request, each gated by an `isMember`
 *  check so clients need only include the categories they want removed:
 *
 *  - **streams** — named fan-out queues: `server`, `ledger`, `manifests`,
 *    `transactions`, `transactions_proposed` (alias: `rt_transactions`),
 *    `validations`, `peer_status`, `consensus`. Note: `book_changes` has no
 *    unsubscribe path; those subscriptions are implicitly torn down when the
 *    connection closes.
 *  - **accounts** — validated transaction notifications for specific accounts.
 *  - **accounts_proposed** (alias: `rt_accounts`) — proposed transaction
 *    notifications.
 *  - **books** — order book updates. `both`/`both_sides` (deprecated) triggers
 *    a second `unsubBook` call with the reversed book to match the symmetric
 *    subscribe behavior. Unlike `doSubscribe`, no `isConsistent` check is
 *    performed — a subscription must be removable regardless of current state.
 *  - **account_history_tx_stream** — experimental historical + live replay;
 *    the optional `stop_history_tx_only` boolean halts historical replay while
 *    preserving the live account subscription.
 *
 *  Subscriber identity is determined as follows: if the request carries a
 *  `url` parameter, the existing `RPCSub` HTTP-push object is looked up by URL
 *  (requires `Role::ADMIN`); if not found the handler returns an empty success
 *  rather than an error — unsubscribing from a URL that was never registered
 *  is a no-op. Otherwise `context.infoSub` (the live WebSocket `InfoSub`) is
 *  used. A call with neither is rejected with `rpcINVALID_PARAMS`.
 *
 *  When the `url` path is taken, `tryRemoveRpcSub` is called only *after* all
 *  individual `unsub*` operations complete. This ensures the subscriber is
 *  fully drained before the URL registration is cleaned up.
 *
 *  @param context  RPC dispatch context carrying the parsed request params,
 *      the `InfoSub` for the WebSocket connection (if any), the `NetworkOPs`
 *      reference used for all unsub calls, and the resolved `Role`.
 *  @return Empty JSON object on success. Returns an `rpcError` object on the
 *      first validation failure encountered:
 *      `rpcINVALID_PARAMS` for structural problems,
 *      `rpcNO_PERMISSION` for the admin-only URL path,
 *      `rpcSTREAM_MALFORMED` for unknown or non-string stream names,
 *      `rpcACT_MALFORMED` for unparseable account addresses,
 *      `rpcBAD_MARKET` for degenerate (same-asset) order books,
 *      `rpcDOMAIN_MALFORMED` for malformed domain hex.
 *  @note `peer_status` requires `Role::ADMIN` to *subscribe* but no role
 *      check to *unsubscribe* — removal is always safe regardless of
 *      permission level.
 *  @note `book_changes` cannot be unsubscribed via this handler; it is torn
 *      down automatically on connection close.
 *  @see doSubscribe
 */
json::Value
doUnsubscribe(RPC::JsonContext& context)
{
    InfoSub::pointer ispSub;
    json::Value jvResult(json::ValueType::Object);
    bool removeUrl{false};

    if (!context.infoSub && !context.params.isMember(jss::url))
    {
        return rpcError(RpcInvalidParams);
    }

    if (context.params.isMember(jss::url))
    {
        if (context.role != Role::ADMIN)
            return rpcError(RpcNoPermission);

        std::string const strUrl = context.params[jss::url].asString();
        ispSub = context.netOps.findRpcSub(strUrl);
        if (!ispSub)
            return jvResult;
        removeUrl = true;
    }
    else
    {
        ispSub = context.infoSub;
    }

    if (context.params.isMember(jss::streams))
    {
        if (!context.params[jss::streams].isArray())
            return rpcError(RpcInvalidParams);

        for (auto& it : context.params[jss::streams])
        {
            if (!it.isString())
                return rpcError(RpcStreamMalformed);

            std::string const streamName = it.asString();
            if (streamName == "server")
            {
                context.netOps.unsubServer(ispSub->getSeq());
            }
            else if (streamName == "ledger")
            {
                context.netOps.unsubLedger(ispSub->getSeq());
            }
            else if (streamName == "manifests")
            {
                context.netOps.unsubManifests(ispSub->getSeq());
            }
            else if (streamName == "transactions")
            {
                context.netOps.unsubTransactions(ispSub->getSeq());
            }
            else if (
                streamName == "transactions_proposed" ||
                streamName == "rt_transactions")
            {
                context.netOps.unsubRTTransactions(ispSub->getSeq());
            }
            else if (streamName == "validations")
            {
                context.netOps.unsubValidations(ispSub->getSeq());
            }
            else if (streamName == "peer_status")
            {
                context.netOps.unsubPeerStatus(ispSub->getSeq());
            }
            else if (streamName == "consensus")
            {
                context.netOps.unsubConsensus(ispSub->getSeq());
            }
            else
            {
                return rpcError(RpcStreamMalformed);
            }
        }
    }

    auto accountsProposed = context.params.isMember(jss::accounts_proposed)
        ? jss::accounts_proposed
        : jss::rt_accounts;
    if (context.params.isMember(accountsProposed))
    {
        if (!context.params[accountsProposed].isArray())
            return rpcError(RpcInvalidParams);

        auto ids = RPC::parseAccountIds(context.params[accountsProposed]);
        if (ids.empty())
            return rpcError(RpcActMalformed);
        context.netOps.unsubAccount(ispSub, ids, true);
    }

    if (context.params.isMember(jss::accounts))
    {
        if (!context.params[jss::accounts].isArray())
            return rpcError(RpcInvalidParams);

        auto ids = RPC::parseAccountIds(context.params[jss::accounts]);
        if (ids.empty())
            return rpcError(RpcActMalformed);
        context.netOps.unsubAccount(ispSub, ids, false);
    }

    if (context.params.isMember(jss::account_history_tx_stream))
    {
        auto const& req = context.params[jss::account_history_tx_stream];
        if (!req.isMember(jss::account) || !req[jss::account].isString())
            return rpcError(RpcInvalidParams);

        auto const id = parseBase58<AccountID>(req[jss::account].asString());
        if (!id)
            return rpcError(RpcInvalidParams);

        bool stopHistoryOnly = false;
        if (req.isMember(jss::stop_history_tx_only))
        {
            if (!req[jss::stop_history_tx_only].isBool())
                return rpcError(RpcInvalidParams);
            stopHistoryOnly = req[jss::stop_history_tx_only].asBool();
        }
        context.netOps.unsubAccountHistory(ispSub, *id, stopHistoryOnly);

        JLOG(context.j.debug()) << "doUnsubscribe: account_history_tx_stream: " << toBase58(*id)
                                << " stopHistoryOnly=" << (stopHistoryOnly ? "true" : "false");
    }

    if (context.params.isMember(jss::books))
    {
        if (!context.params[jss::books].isArray())
            return rpcError(RpcInvalidParams);

        for (auto& jv : context.params[jss::books])
        {
            if (!jv.isObject() || !jv.isMember(jss::taker_pays) || !jv.isMember(jss::taker_gets) ||
                !jv[jss::taker_pays].isObjectOrNull() || !jv[jss::taker_gets].isObjectOrNull())
            {
                return rpcError(RpcInvalidParams);
            }

            Book book;

            if (auto const err = RPC::parseSubUnsubJson(book.in, jv, jss::taker_pays, context.j);
                err != RpcSuccess)
                return rpcError(err);

            if (auto const err = RPC::parseSubUnsubJson(book.out, jv, jss::taker_gets, context.j);
                err != RpcSuccess)
                return rpcError(err);

            if (book.in == book.out)
            {
                JLOG(context.j.info()) << "taker_gets same as taker_pays.";
                return rpcError(RpcBadMarket);
            }

            if (jv.isMember(jss::domain))
            {
                uint256 domain;
                if (!jv[jss::domain].isString() || !domain.parseHex(jv[jss::domain].asString()))
                {
                    return rpcError(RpcDomainMalformed);
                }

                book.domain = domain;
            }

            context.netOps.unsubBook(ispSub->getSeq(), book);

            if ((jv.isMember(jss::both) && jv[jss::both].asBool()) ||
                (jv.isMember(jss::both_sides) && jv[jss::both_sides].asBool()))
            {
                context.netOps.unsubBook(ispSub->getSeq(), reversed(book));
            }
        }
    }

    if (removeUrl)
    {
        context.netOps.tryRemoveRpcSub(context.params[jss::url].asString());
    }

    return jvResult;
}

}  // namespace xrpl
