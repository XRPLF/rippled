#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Json::Value
doUnsubscribe(RPC::JsonContext& context)
{
    InfoSub::pointer ispSub;
    Json::Value jvResult(Json::objectValue);
    bool removeUrl{false};

    if (!context.infoSub && !context.params.isMember(jss::url))
    {
        // Must be a JSON-RPC call.
        return rpcError(rpcINVALID_PARAMS);
    }

    if (context.params.isMember(jss::url))
    {
        if (context.role != Role::ADMIN)
            return rpcError(rpcNO_PERMISSION);

        std::string strUrl = context.params[jss::url].asString();
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
            return rpcError(rpcINVALID_PARAMS);

        for (auto& it : context.params[jss::streams])
        {
            if (!it.isString())
                return rpcError(rpcSTREAM_MALFORMED);

            std::string streamName = it.asString();
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
                return rpcError(rpcSTREAM_MALFORMED);
            }
        }
    }

    auto accountsProposed = context.params.isMember(jss::accounts_proposed)
        ? jss::accounts_proposed
        : jss::rt_accounts;  // DEPRECATED
    if (context.params.isMember(accountsProposed))
    {
        if (!context.params[accountsProposed].isArray())
            return rpcError(rpcINVALID_PARAMS);

        auto ids = RPC::parseAccountIds(context.params[accountsProposed]);
        if (ids.empty())
            return rpcError(rpcACT_MALFORMED);
        context.netOps.unsubAccount(ispSub, ids, true);
    }

    if (context.params.isMember(jss::accounts))
    {
        if (!context.params[jss::accounts].isArray())
            return rpcError(rpcINVALID_PARAMS);

        auto ids = RPC::parseAccountIds(context.params[jss::accounts]);
        if (ids.empty())
            return rpcError(rpcACT_MALFORMED);
        context.netOps.unsubAccount(ispSub, ids, false);
    }

    if (context.params.isMember(jss::account_history_tx_stream))
    {
        auto const& req = context.params[jss::account_history_tx_stream];
        if (!req.isMember(jss::account) || !req[jss::account].isString())
            return rpcError(rpcINVALID_PARAMS);

        auto const id = parseBase58<AccountID>(req[jss::account].asString());
        if (!id)
            return rpcError(rpcINVALID_PARAMS);

        bool stopHistoryOnly = false;
        if (req.isMember(jss::stop_history_tx_only))
        {
            if (!req[jss::stop_history_tx_only].isBool())
                return rpcError(rpcINVALID_PARAMS);
            stopHistoryOnly = req[jss::stop_history_tx_only].asBool();
        }
        context.netOps.unsubAccountHistory(ispSub, *id, stopHistoryOnly);

        JLOG(context.j.debug())
            << "doUnsubscribe: account_history_tx_stream: " << toBase58(*id)
            << " stopHistoryOnly=" << (stopHistoryOnly ? "true" : "false");
    }

    if (context.params.isMember(jss::books))
    {
        if (!context.params[jss::books].isArray())
            return rpcError(rpcINVALID_PARAMS);

        for (auto& jv : context.params[jss::books])
        {
            if (!jv.isObject() || !jv.isMember(jss::taker_pays) ||
                !jv.isMember(jss::taker_gets) ||
                !jv[jss::taker_pays].isObjectOrNull() ||
                !jv[jss::taker_gets].isObjectOrNull())
            {
                return rpcError(rpcINVALID_PARAMS);
            }

            Json::Value taker_pays = jv[jss::taker_pays];
            Json::Value taker_gets = jv[jss::taker_gets];

            Book book;

            // Parse mandatory currency.
            if (!taker_pays.isMember(jss::currency) ||
                !to_currency(
                    book.in.currency, taker_pays[jss::currency].asString()))
            {
                JLOG(context.j.info()) << "Bad taker_pays currency.";
                return rpcError(rpcSRC_CUR_MALFORMED);
            }
            // Parse optional issuer.
            else if (
                ((taker_pays.isMember(jss::issuer)) &&
                 (!taker_pays[jss::issuer].isString() ||
                  !to_issuer(
                      book.in.account, taker_pays[jss::issuer].asString())))
                // Don't allow illegal issuers.
                || !isConsistent(book.in) || noAccount() == book.in.account)
            {
                JLOG(context.j.info()) << "Bad taker_pays issuer.";

                return rpcError(rpcSRC_ISR_MALFORMED);
            }

            // Parse mandatory currency.
            if (!taker_gets.isMember(jss::currency) ||
                !to_currency(
                    book.out.currency, taker_gets[jss::currency].asString()))
            {
                JLOG(context.j.info()) << "Bad taker_gets currency.";

                return rpcError(rpcDST_AMT_MALFORMED);
            }
            // Parse optional issuer.
            else if (
                ((taker_gets.isMember(jss::issuer)) &&
                 (!taker_gets[jss::issuer].isString() ||
                  !to_issuer(
                      book.out.account, taker_gets[jss::issuer].asString())))
                // Don't allow illegal issuers.
                || !isConsistent(book.out) || noAccount() == book.out.account)
            {
                JLOG(context.j.info()) << "Bad taker_gets issuer.";

                return rpcError(rpcDST_ISR_MALFORMED);
            }

            if (book.in == book.out)
            {
                JLOG(context.j.info()) << "taker_gets same as taker_pays.";
                return rpcError(rpcBAD_MARKET);
            }

            if (jv.isMember(jss::domain))
            {
                uint256 domain;
                if (!jv[jss::domain].isString() ||
                    !domain.parseHex(jv[jss::domain].asString()))
                {
                    return rpcError(rpcDOMAIN_MALFORMED);
                }
                else
                {
                    book.domain = domain;
                }
            }

            context.netOps.unsubBook(ispSub->getSeq(), book);

            // both_sides is deprecated.
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

}  // namespace ripple
