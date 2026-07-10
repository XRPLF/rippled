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

json::Value
doUnsubscribe(RPC::JsonContext& context)
{
    InfoSub::pointer ispSub;
    json::Value jvResult(json::ValueType::Object);
    bool removeUrl{false};

    if (!context.infoSub && !context.params.isMember(jss::url))
    {
        // Must be a JSON-RPC call.
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
                streamName == "rt_transactions")  // DEPRECATED
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
        : jss::rt_accounts;  // DEPRECATED
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

            if (!context.netOps.unsubBook(ispSub, book))
            {
                JLOG(context.j.debug())
                    << "doUnsubscribe: book not subscribed (no-op for seq=" << ispSub->getSeq()
                    << ")";
            }

            // both_sides is deprecated.
            if ((jv.isMember(jss::both) && jv[jss::both].asBool()) ||
                (jv.isMember(jss::both_sides) && jv[jss::both_sides].asBool()))
            {
                if (!context.netOps.unsubBook(ispSub, reversed(book)))
                {
                    JLOG(context.j.debug())
                        << "doUnsubscribe: reversed book not subscribed (no-op for seq="
                        << ispSub->getSeq() << ")";
                }
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
