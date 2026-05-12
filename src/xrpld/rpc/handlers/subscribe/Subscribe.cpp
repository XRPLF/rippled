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

json::Value
doSubscribe(RPC::JsonContext& context)
{
    InfoSub::pointer ispSub;
    json::Value jvResult(json::ValueType::Object);

    if (!context.infoSub && !context.params.isMember(jss::url))
    {
        // Must be a JSON-RPC call.
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

        context.loadType = Resource::kFeeMediumBurdenRpc;
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
                context.loadType = Resource::kFeeMediumBurdenRpc;
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
                            RPC::Tuning::kBookOffers.rDefault,
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
