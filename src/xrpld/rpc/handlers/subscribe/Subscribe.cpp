/** @file
 *  Implements the `subscribe` RPC/WebSocket command handler.
 *
 *  Registers a client connection as a listener on named event streams, account
 *  filters, or order books. The symmetric counterpart `doUnsubscribe` in
 *  Unsubscribe.cpp tears down these registrations using identical parameter
 *  parsing logic.
 */

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/RPCSub.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/InfoSub.h>
#include <xrpl/server/NetworkOPs.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace xrpl {

/** Register event subscriptions for a WebSocket or HTTP-callback client.
 *
 *  Resolves the subscriber identity, then processes up to five independent
 *  subscription categories from the request:
 *
 *  - **streams** — named fan-out queues (`ledger`, `transactions`,
 *    `transactions_proposed`, `validations`, `manifests`, `server`,
 *    `book_changes`, `consensus`, `peer_status`).
 *  - **accounts** — validated transaction notifications for specific accounts.
 *  - **accounts_proposed** — proposed (pre-validation) transaction notifications.
 *  - **books** — order book updates, with an optional current-state snapshot.
 *  - **account_history_tx_stream** — experimental historical + live replay for
 *    one account (requires `useTxTables()`).
 *
 *  Subscriber identity is determined as follows: if the request contains a
 *  `url` parameter an `RPCSub` HTTP-push object is created (or reused from the
 *  server registry); otherwise `context.infoSub` — the WebSocket connection's
 *  `InfoSub` — is used directly. The `url` path requires `Role::ADMIN`.
 *
 *  For book subscriptions the handler registers the live listener *before*
 *  reading the snapshot, eliminating the race where an offer update fires
 *  between the snapshot read and the subscription registration. Clients may
 *  therefore observe duplicates between the snapshot and the live stream, but
 *  will never miss an update.
 *
 *  Every validation step returns immediately on failure without partial
 *  side effects, so the client either gets all requested subscriptions or
 *  an error with no state changed.
 *
 *  @param context  RPC dispatch context carrying the parsed request params,
 *      the `InfoSub` for the WebSocket connection (if any), the `NetworkOPs`
 *      reference used for all sub/unsub calls, and the resource consumer.
 *  @return JSON object with the initial subscription state; includes current
 *      ledger info for `ledger` stream, offer snapshots under `offers`/
 *      `bids`/`asks` when `snapshot` is requested, and a `warning` field
 *      for `account_history_tx_stream`. Returns an `rpcError` object on any
 *      validation failure.
 *  @note Callers without an active WebSocket connection (plain HTTP) must
 *      supply a `url` parameter; without either, `rpcINVALID_PARAMS` is
 *      returned immediately.
 *  @note `rt_transactions` is a deprecated alias for `transactions_proposed`;
 *      `rt_accounts` is a deprecated alias for `accounts_proposed`;
 *      `both_sides` is a deprecated alias for `both`; `state_now` is a
 *      deprecated alias for `snapshot`. All aliases remain supported.
 *  @note The `book_changes` stream has no unsubscribe path; once registered,
 *      it cannot be removed for the lifetime of the connection.
 *  @note Book snapshot reads charge `feeMediumBurdenRPC` to the resource
 *      consumer, as does `account_history_tx_stream` subscription.
 *  @see doUnsubscribe
 */
json::Value
doSubscribe(RPC::JsonContext& context)
{
    InfoSub::pointer ispSub;
    json::Value jvResult(json::ValueType::Object);

    if (!context.infoSub && !context.params.isMember(jss::url))
    {
        JLOG(context.j.info()) << "doSubscribe: RPC subscribe requires a url";
        return rpcError(RpcInvalidParams);
    }

    if (context.params.isMember(jss::url))
    {
        if (context.role != Role::ADMIN)
            return rpcError(RpcNoPermission);

        std::string const strUrl = context.params[jss::url].asString();
        std::string strUsername = context.params.isMember(jss::url_username)
            ? context.params[jss::url_username].asString()
            : "";
        std::string strPassword = context.params.isMember(jss::url_password)
            ? context.params[jss::url_password].asString()
            : "";

        // DEPRECATED
        if (context.params.isMember(jss::username))
            strUsername = context.params[jss::username].asString();

        // DEPRECATED
        if (context.params.isMember(jss::password))
            strPassword = context.params[jss::password].asString();

        ispSub = context.netOps.findRpcSub(strUrl);
        if (!ispSub)
        {
            JLOG(context.j.debug()) << "doSubscribe: building: " << strUrl;
            try
            {
                auto rspSub = makeRPCSub(
                    context.app.getOPs(),
                    context.app.getIOContext(),
                    context.app.getJobQueue(),
                    strUrl,
                    strUsername,
                    strPassword,
                    context.app);
                ispSub =
                    context.netOps.addRpcSub(strUrl, std::dynamic_pointer_cast<InfoSub>(rspSub));
            }
            catch (std::runtime_error const& ex)
            {
                return RPC::makeParamError(ex.what());
            }
        }
        else
        {
            JLOG(context.j.trace()) << "doSubscribe: reusing: " << strUrl;

            if (auto rpcSub = std::dynamic_pointer_cast<RPCSub>(ispSub))
            {
                // Why do we need to check isMember against jss::username and
                // jss::password here instead of just setting the username and
                // the password? What about url_username and url_password?
                if (context.params.isMember(jss::username))
                    rpcSub->setUsername(strUsername);

                if (context.params.isMember(jss::password))
                    rpcSub->setPassword(strPassword);
            }
        }
    }
    else
    {
        ispSub = context.infoSub;
    }
    ispSub->setApiVersion(context.apiVersion);

    if (context.params.isMember(jss::streams))
    {
        if (!context.params[jss::streams].isArray())
        {
            JLOG(context.j.info()) << "doSubscribe: streams requires an array.";
            return rpcError(RpcInvalidParams);
        }

        for (auto const& it : context.params[jss::streams])
        {
            if (!it.isString())
                return rpcError(RpcStreamMalformed);

            std::string const streamName = it.asString();
            if (streamName == "server")
            {
                context.netOps.subServer(ispSub, jvResult, context.role == Role::ADMIN);
            }
            else if (streamName == "ledger")
            {
                context.netOps.subLedger(ispSub, jvResult);
            }
            else if (streamName == "book_changes")
            {
                context.netOps.subBookChanges(ispSub);
            }
            else if (streamName == "manifests")
            {
                context.netOps.subManifests(ispSub);
            }
            else if (streamName == "transactions")
            {
                context.netOps.subTransactions(ispSub);
            }
            else if (
                streamName == "transactions_proposed" ||
                streamName == "rt_transactions")  // DEPRECATED
            {
                context.netOps.subRTTransactions(ispSub);
            }
            else if (streamName == "validations")
            {
                context.netOps.subValidations(ispSub);
            }
            else if (streamName == "peer_status")
            {
                if (context.role != Role::ADMIN)
                    return rpcError(RpcNoPermission);
                context.netOps.subPeerStatus(ispSub);
            }
            else if (streamName == "consensus")
            {
                context.netOps.subConsensus(ispSub);
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
        context.netOps.subAccount(ispSub, ids, true);
    }

    if (context.params.isMember(jss::accounts))
    {
        if (!context.params[jss::accounts].isArray())
            return rpcError(RpcInvalidParams);

        auto ids = RPC::parseAccountIds(context.params[jss::accounts]);
        if (ids.empty())
            return rpcError(RpcActMalformed);
        context.netOps.subAccount(ispSub, ids, false);
        JLOG(context.j.debug()) << "doSubscribe: accounts: " << ids.size();
    }

    if (context.params.isMember(jss::account_history_tx_stream))
    {
        if (!context.app.config().useTxTables())
            return rpcError(RpcNotEnabled);

        context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;
        auto const& req = context.params[jss::account_history_tx_stream];
        if (!req.isMember(jss::account) || !req[jss::account].isString())
            return rpcError(RpcInvalidParams);

        auto const id = parseBase58<AccountID>(req[jss::account].asString());
        if (!id)
            return rpcError(RpcInvalidParams);

        if (auto result = context.netOps.subAccountHistory(ispSub, *id); result != RpcSuccess)
        {
            return rpcError(result);
        }

        jvResult[jss::warning] =
            "account_history_tx_stream is an experimental feature and likely "
            "to be removed in the future";
        JLOG(context.j.debug()) << "doSubscribe: account_history_tx_stream: " << toBase58(*id);
    }

    if (context.params.isMember(jss::books))
    {
        if (!context.params[jss::books].isArray())
            return rpcError(RpcInvalidParams);

        for (auto& j : context.params[jss::books])
        {
            if (!j.isObject() || !j.isMember(jss::taker_pays) || !j.isMember(jss::taker_gets) ||
                !j[jss::taker_pays].isObjectOrNull() || !j[jss::taker_gets].isObjectOrNull())
                return rpcError(RpcInvalidParams);

            Book book;

            if (auto const err = RPC::parseSubUnsubJson(book.in, j, jss::taker_pays, context.j);
                err != RpcSuccess)
                return rpcError(err);

            if (auto const err = RPC::parseSubUnsubJson(book.out, j, jss::taker_gets, context.j);
                err != RpcSuccess)
                return rpcError(err);

            if (book.in == book.out)
            {
                JLOG(context.j.info()) << "taker_gets same as taker_pays.";
                return rpcError(RpcBadMarket);
            }

            std::optional<AccountID> takerID;

            if (j.isMember(jss::taker))
            {
                if (!j[jss::taker].isString())
                    return rpcError(RpcActMalformed);
                takerID = parseBase58<AccountID>(j[jss::taker].asString());
                if (!takerID)
                    return rpcError(RpcActMalformed);
            }

            if (j.isMember(jss::domain))
            {
                uint256 domain;
                if (!j[jss::domain].isString() || !domain.parseHex(j[jss::domain].asString()))
                {
                    return rpcError(RpcDomainMalformed);
                }

                book.domain = domain;
            }

            if (!isConsistent(book))
            {
                JLOG(context.j.warn()) << "Bad market: " << book;
                return rpcError(RpcBadMarket);
            }

            context.netOps.subBook(ispSub, book);

            // both_sides is deprecated.
            bool const both = (j.isMember(jss::both) && j[jss::both].asBool()) ||
                (j.isMember(jss::both_sides) && j[jss::both_sides].asBool());

            if (both)
                context.netOps.subBook(ispSub, reversed(book));

            // state_now is deprecated.
            if ((j.isMember(jss::snapshot) && j[jss::snapshot].asBool()) ||
                (j.isMember(jss::state_now) && j[jss::state_now].asBool()))
            {
                context.loadType = Resource::kFEE_MEDIUM_BURDEN_RPC;
                std::shared_ptr<ReadView const> lpLedger =
                    context.app.getLedgerMaster().getPublishedLedger();
                if (lpLedger)
                {
                    json::Value const jvMarker = json::Value(json::ValueType::Null);
                    json::Value jvOffers(json::ValueType::Object);

                    auto add = [&](json::StaticString field) {
                        context.netOps.getBookPage(
                            lpLedger,
                            field == jss::asks ? reversed(book) : book,
                            takerID ? *takerID : noAccount(),
                            false,
                            RPC::Tuning::kBOOK_OFFERS.rDefault,
                            jvMarker,
                            jvOffers);

                        if (jvResult.isMember(field))
                        {
                            json::Value& results(jvResult[field]);
                            for (auto const& e : jvOffers[jss::offers])
                                results.append(e);
                        }
                        else
                        {
                            jvResult[field] = jvOffers[jss::offers];
                        }
                    };

                    if (both)
                    {
                        add(jss::bids);
                        add(jss::asks);
                    }
                    else
                    {
                        add(jss::offers);
                    }
                }
            }
        }
    }

    return jvResult;
}

}  // namespace xrpl
