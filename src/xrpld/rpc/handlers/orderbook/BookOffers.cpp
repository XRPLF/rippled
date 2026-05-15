#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/Job.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/NetworkOPs.h>

#include <memory>
#include <optional>

namespace xrpl {

std::optional<json::Value>
validateTakerJSON(json::Value const& taker, json::StaticString const& name)
{
    if (!taker.isMember(jss::currency) && !taker.isMember(jss::mpt_issuance_id))
    {
        return RPC::missingFieldError((boost::format("%s.currency") % name.cStr()).str());
    }

    if (taker.isMember(jss::mpt_issuance_id) &&
        (taker.isMember(jss::currency) || taker.isMember(jss::issuer)))
    {
        return RPC::invalidFieldError(name.cStr());
    }

    if ((taker.isMember(jss::currency) && !taker[jss::currency].isString()) ||
        (taker.isMember(jss::mpt_issuance_id) && !taker[jss::mpt_issuance_id].isString()))
    {
        return RPC::expectedFieldError(
            (boost::format("%s.currency") % name.cStr()).str(), "string");
    }

    return std::nullopt;
}

std::optional<json::Value>
parseTakerAssetJSON(
    Asset& asset,
    json::Value const& taker,
    json::StaticString const& name,
    beast::Journal j)
{
    auto const assetError = [&]() {
        if (name == jss::taker_pays)
            return RpcSrcCurMalformed;
        return RpcDstAmtMalformed;
    }();

    if (taker.isMember(jss::currency))
    {
        Issue issue = xrpIssue();

        if (!toCurrency(issue.currency, taker[jss::currency].asString()))
        {
            JLOG(j.info()) << boost::format("Bad %s currency.") % name.cStr();
            return RPC::makeError(
                assetError,
                (boost::format("Invalid field '%s.currency', bad currency.") % name.cStr()).str());
        }
        asset = issue;
    }
    else if (taker.isMember(jss::mpt_issuance_id))
    {
        MPTID mptid;
        if (!mptid.parseHex(taker[jss::mpt_issuance_id].asString()))
        {
            return RPC::makeError(
                assetError,
                (boost::format("Invalid field '%s.mpt_issuance_id'") % name.cStr()).str());
        }
        asset = mptid;
    }

    return std::nullopt;
}

std::optional<json::Value>
parseTakerIssuerJSON(
    Asset& asset,
    json::Value const& taker,
    json::StaticString const& name,
    beast::Journal j)
{
    auto const issuerError = [&]() {
        if (name == jss::taker_pays)
            return RpcSrcIsrMalformed;
        return RpcDstIsrMalformed;
    }();

    if (taker.isMember(jss::currency))
    {
        Issue& issue = asset.get<Issue>();

        if (taker.isMember(jss::issuer))
        {
            if (!taker[jss::issuer].isString())
            {
                return RPC::expectedFieldError(
                    (boost::format("%s.issuer") % name.cStr()).str(), "string");
            }

            if (!toIssuer(issue.account, taker[jss::issuer].asString()))
            {
                return RPC::makeError(
                    issuerError,
                    (boost::format("Invalid field '%s.issuer', bad issuer.") % name.cStr()).str());
            }

            if (issue.account == noAccount())
            {
                return RPC::makeError(
                    issuerError,
                    (boost::format("Invalid field '%s.issuer', bad issuer account one.") %
                     name.cStr())
                        .str());
            }
        }
        else
        {
            issue.account = xrpAccount();
        }

        if (isXRP(issue.currency) && !isXRP(issue.account))
        {
            return RPC::makeError(
                issuerError,
                (boost::format(
                     "Unneeded field '%s.issuer' for XRP currency "
                     "specification.") %
                 name.cStr())
                    .str());
        }

        if (!isXRP(issue.currency) && isXRP(issue.account))
        {
            return RPC::makeError(
                issuerError,
                (boost::format("Invalid field '%s.issuer', expected non-XRP issuer.") % name.cStr())
                    .str());
        }
    }

    return std::nullopt;
}

json::Value
doBookOffers(RPC::JsonContext& context)
{
    // VFALCO TODO Here is a terrible place for this kind of business
    //             logic. It needs to be moved elsewhere and documented,
    //             and encapsulated into a function.
    if (context.app.getJobQueue().getJobCountGE(JtClient) > 200)
        return rpcError(RpcTooBusy);

    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    if (!context.params.isMember(jss::taker_pays))
        return RPC::missingFieldError(jss::taker_pays);

    if (!context.params.isMember(jss::taker_gets))
        return RPC::missingFieldError(jss::taker_gets);

    json::Value const& takerPays = context.params[jss::taker_pays];
    json::Value const& takerGets = context.params[jss::taker_gets];

    if (!takerPays.isObjectOrNull())
        return RPC::objectFieldError(jss::taker_pays);

    if (!takerGets.isObjectOrNull())
        return RPC::objectFieldError(jss::taker_gets);

    if (auto const err = validateTakerJSON(takerPays, jss::taker_pays))
        return *err;

    if (auto const err = validateTakerJSON(takerGets, jss::taker_gets))
        return *err;

    Book book;

    if (auto const err = parseTakerAssetJSON(book.in, takerPays, jss::taker_pays, context.j))
        return *err;

    if (auto const err = parseTakerAssetJSON(book.out, takerGets, jss::taker_gets, context.j))
        return *err;

    if (auto const err = parseTakerIssuerJSON(book.in, takerPays, jss::taker_pays, context.j))
        return *err;

    if (auto const err = parseTakerIssuerJSON(book.out, takerGets, jss::taker_gets, context.j))
        return *err;

    std::optional<AccountID> takerID;
    if (context.params.isMember(jss::taker))
    {
        if (!context.params[jss::taker].isString())
            return RPC::expectedFieldError(jss::taker, "string");

        takerID = parseBase58<AccountID>(context.params[jss::taker].asString());
        if (!takerID)
            return RPC::invalidFieldError(jss::taker);
    }

    std::optional<uint256> domain;
    if (context.params.isMember(jss::domain))
    {
        uint256 num;
        if (!context.params[jss::domain].isString() ||
            !num.parseHex(context.params[jss::domain].asString()))
        {
            return RPC::makeError(RpcDomainMalformed, "Unable to parse domain.");
        }

        domain = num;
    }

    if (book.in == book.out)
    {
        JLOG(context.j.info()) << "taker_gets same as taker_pays.";
        return RPC::makeError(RpcBadMarket);
    }

    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kBookOffers, context))
        return *err;

    bool const bProof(context.params.isMember(jss::proof));

    json::Value const jvMarker(
        context.params.isMember(jss::marker) ? context.params[jss::marker]
                                             : json::Value(json::ValueType::Null));

    context.netOps.getBookPage(
        lpLedger,
        {book.in, book.out, domain},
        takerID ? *takerID : beast::kZero,
        bProof,
        limit,
        jvMarker,
        jvResult);

    context.loadType = Resource::kFeeMediumBurdenRpc;

    return jvResult;
}

}  // namespace xrpl
