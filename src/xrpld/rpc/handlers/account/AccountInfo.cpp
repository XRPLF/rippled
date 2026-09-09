#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace xrpl {

/**
 * @brief Injects JSON describing a ledger entry.
 *
 * @param jv The JSON value to populate.
 * @param sle The ledger entry to describe.
 *
 * @details
 * Populates the provided JSON value with the description of the specified
 * ledger entry. If the entry is an account root and contains an email hash,
 * adds a 'urlgravatar' field with the corresponding Gravatar URL.
 * If the entry is not an account root, sets the 'Invalid' field to true.
 */
void
injectSLE(json::Value& jv, SLE const& sle)
{
    jv = sle.getJson(JsonOptions::Values::None);
    XRPL_ASSERT(sle.getType() == ltACCOUNT_ROOT, "xrpl::injectSLE : sle is account root");
    if (sle.isFieldPresent(sfEmailHash))
    {
        auto const& hash = sle.getFieldH128(sfEmailHash);
        Blob const b(hash.begin(), hash.end());
        std::string md5 = strHex(makeSlice(b));
        md5 = toLower(md5);
        // VFALCO TODO Give a name to this constant and move it
        //             to a more visible location.
        jv[jss::urlgravatar] = std::format("https://www.gravatar.com/avatar/{}", md5);
    }
}

// {
//   account: <ident>,
//   ledger_hash : <ledger>
//   ledger_index : <ledger_index>
//   signer_lists : <bool> // optional (default false)
//                         //   if true return SignerList(s).
//   queue : <bool>        // optional (default false)
//                         //   if true return information about transactions
//                         //   in the current TxQ, only if the requested
//                         //   ledger is open. Otherwise if true, returns an
//                         //   error.
// }

// TODO(tom): what is that "default"?
json::Value
doAccountInfo(rpc::JsonContext& context)
{
    auto& params = context.params;

    std::string strIdent;
    if (params.isMember(jss::account))
    {
        if (!params[jss::account].isString())
            return rpc::invalidFieldError(jss::account);
        strIdent = params[jss::account].asString();
    }
    else if (params.isMember(jss::ident))
    {
        if (!params[jss::ident].isString())
            return rpc::invalidFieldError(jss::ident);
        strIdent = params[jss::ident].asString();
    }
    else
    {
        return rpc::missingFieldError(jss::account);
    }

    std::shared_ptr<ReadView const> ledger;
    auto result = rpc::lookupLedger(ledger, context);

    if (!ledger)
        return result;

    // Get info on account.
    auto id = parseBase58<AccountID>(strIdent);
    if (!id)
    {
        rpc::injectError(RpcActMalformed, result);
        return result;
    }
    auto const accountID{id.value()};

    // Flags that are always reported.
    static constexpr auto kAccountRootFlags =
        std::to_array<std::pair<std::string_view, LedgerSpecificFlags>>(
            {{"allowTrustLineClawback", lsfAllowTrustLineClawback},
             {"defaultRipple", lsfDefaultRipple},
             {"depositAuth", lsfDepositAuth},
             {"disableMasterKey", lsfDisableMaster},
             {"disallowIncomingCheck", lsfDisallowIncomingCheck},
             {"disallowIncomingNFTokenOffer", lsfDisallowIncomingNFTokenOffer},
             {"disallowIncomingPayChan", lsfDisallowIncomingPayChan},
             {"disallowIncomingTrustline", lsfDisallowIncomingTrustline},
             {"disallowIncomingXRP", lsfDisallowXRP},
             {"globalFreeze", lsfGlobalFreeze},
             {"noFreeze", lsfNoFreeze},
             {"passwordSpent", lsfPasswordSpent},
             {"requireAuthorization", lsfRequireAuth},
             {"requireDestinationTag", lsfRequireDestTag}});

    // Flags that are only reported when their amendment is enabled. This can't be `constexpr`,
    // since the amendment IDs are computed at runtime.
    static auto const kAmendmentGatedFlags =
        std::to_array<std::tuple<std::string_view, LedgerSpecificFlags, uint256 const&>>(
            {{"allowTrustLineLocking", lsfAllowTrustLineLocking, featureTokenEscrow}});

    // Every `AccountRoot` flag must be reported by `account_info`, so if a new flag is added, it
    // needs to be added to one of the arrays above. This can't be a `static_assert` because
    // `getAccountRootFlags()` builds its map at runtime.
    XRPL_ASSERT_PARTS(
        kAccountRootFlags.size() + kAmendmentGatedFlags.size() == getAccountRootFlags().size(),
        "xrpl::doAccountInfo",
        "number of account flags");

    auto const sleAccepted = ledger->read(keylet::account(accountID));
    if (sleAccepted)
    {
        auto const queue = params.isMember(jss::queue) && params[jss::queue].asBool();

        if (queue && !ledger->open())
        {
            // It doesn't make sense to request the queue
            // with any closed or validated ledger.
            rpc::injectError(RpcInvalidParams, result);
            return result;
        }

        json::Value jvAccepted(json::ValueType::Object);
        injectSLE(jvAccepted, *sleAccepted);
        result[jss::account_data] = jvAccepted;

        json::Value acctFlags{json::ValueType::Object};
        for (auto const& [name, flag] : kAccountRootFlags)
            acctFlags[name.data()] = sleAccepted->isFlag(flag);

        for (auto const& [name, flag, amendment] : kAmendmentGatedFlags)
        {
            if (ledger->rules().enabled(amendment))
                acctFlags[name.data()] = sleAccepted->isFlag(flag);
        }

        result[jss::account_flags] = std::move(acctFlags);

        auto const pseudoFields = getPseudoAccountFields();
        for (auto const& pseudoField : pseudoFields)
        {
            if (sleAccepted->isFieldPresent(*pseudoField))
            {
                std::string name = pseudoField->fieldName;
                if (name.ends_with("ID"))
                {
                    // Remove the ID suffix from the field name.
                    name = name.substr(0, name.size() - 2);
                    XRPL_ASSERT_PARTS(!name.empty(), "xrpl::doAccountInfo", "name is not empty");
                }
                // ValidPseudoAccounts invariant guarantees that only one field
                // can be set
                result[jss::pseudo_account][jss::type] = name;
                break;
            }
        }

        // The document[https://xrpl.org/account_info.html#account_info] states
        // that signer_lists is a bool, however assigning any string value
        // works. Do not allow this. This check is for api Version 2 onwards
        // only
        if (context.apiVersion > 1u && params.isMember(jss::signer_lists) &&
            !params[jss::signer_lists].isBool())
        {
            rpc::injectError(RpcInvalidParams, result);
            return result;
        }

        // Return SignerList(s) if that is requested.
        if (params.isMember(jss::signer_lists) && params[jss::signer_lists].asBool())
        {
            // We put the SignerList in an array because of an anticipated
            // future when we support multiple signer lists on one account.
            json::Value jvSignerList = json::ValueType::Array;

            // This code will need to be revisited if in the future we support
            // multiple SignerLists on one account.
            auto const sleSigners = ledger->read(keylet::signerList(accountID));
            if (sleSigners)
                jvSignerList.append(sleSigners->getJson(JsonOptions::Values::None));

            // Documentation states this is returned as part of the account_info
            // response, but previously the code put it under account_data. We
            // can move this to the documented location from apiVersion 2
            // onwards.
            if (context.apiVersion == 1)
            {
                result[jss::account_data][jss::signer_lists] = std::move(jvSignerList);
            }
            else
            {
                result[jss::signer_lists] = std::move(jvSignerList);
            }
        }
        // Return queue info if that is requested
        if (queue)
        {
            json::Value jvQueueData = json::ValueType::Object;

            auto const txs = context.app.getTxQ().getAccountTxs(accountID);
            if (!txs.empty())
            {
                jvQueueData[jss::txn_count] = static_cast<json::UInt>(txs.size());

                auto& jvQueueTx = jvQueueData[jss::transactions];
                jvQueueTx = json::ValueType::Array;

                std::uint32_t seqCount = 0;
                std::uint32_t ticketCount = 0;
                std::optional<std::uint32_t> lowestSeq;
                std::optional<std::uint32_t> highestSeq;
                std::optional<std::uint32_t> lowestTicket;
                std::optional<std::uint32_t> highestTicket;
                bool anyAuthChanged = false;
                XRPAmount totalSpend(0);

                // We expect txs to be returned sorted by SeqProxy.  Verify
                // that with a couple of asserts.
                SeqProxy prevSeqProxy = SeqProxy::rawSequence(0);
                for (auto const& tx : txs)
                {
                    json::Value jvTx = json::ValueType::Object;

                    if (tx.seqProxy.isSeq())
                    {
                        XRPL_ASSERT(
                            prevSeqProxy < tx.seqProxy, "doAccountInfo : first sorted proxy");
                        prevSeqProxy = tx.seqProxy;
                        jvTx[jss::seq] = tx.seqProxy.value();
                        ++seqCount;
                        if (!lowestSeq)
                            lowestSeq = tx.seqProxy.value();
                        highestSeq = tx.seqProxy.value();
                    }
                    else
                    {
                        XRPL_ASSERT(
                            prevSeqProxy < tx.seqProxy, "doAccountInfo : second sorted proxy");
                        prevSeqProxy = tx.seqProxy;
                        jvTx[jss::ticket] = tx.seqProxy.value();
                        ++ticketCount;
                        if (!lowestTicket)
                            lowestTicket = tx.seqProxy.value();
                        highestTicket = tx.seqProxy.value();
                    }

                    jvTx[jss::fee_level] = to_string(tx.feeLevel);
                    if (tx.lastValid)
                        jvTx[jss::LastLedgerSequence] = *tx.lastValid;

                    jvTx[jss::fee] = to_string(tx.consequences.fee());
                    auto const spend = tx.consequences.potentialSpend() + tx.consequences.fee();
                    jvTx[jss::max_spend_drops] = to_string(spend);
                    totalSpend += spend;
                    bool const authChanged = tx.consequences.isBlocker();
                    if (authChanged)
                        anyAuthChanged = authChanged;
                    jvTx[jss::auth_change] = authChanged;

                    jvQueueTx.append(std::move(jvTx));
                }

                if (seqCount != 0u)
                    jvQueueData[jss::sequence_count] = seqCount;
                if (ticketCount != 0u)
                    jvQueueData[jss::ticket_count] = ticketCount;
                if (lowestSeq)
                    jvQueueData[jss::lowest_sequence] = *lowestSeq;
                if (highestSeq)
                    jvQueueData[jss::highest_sequence] = *highestSeq;
                if (lowestTicket)
                    jvQueueData[jss::lowest_ticket] = *lowestTicket;
                if (highestTicket)
                    jvQueueData[jss::highest_ticket] = *highestTicket;

                jvQueueData[jss::auth_change_queued] = anyAuthChanged;
                jvQueueData[jss::max_spend_drops_total] = to_string(totalSpend);
            }
            else
            {
                jvQueueData[jss::txn_count] = 0u;
            }

            result[jss::queue_data] = std::move(jvQueueData);
        }
    }
    else
    {
        result[jss::account] = toBase58(accountID);
        rpc::injectError(RpcActNotFound, result);
    }

    return result;
}

}  // namespace xrpl
