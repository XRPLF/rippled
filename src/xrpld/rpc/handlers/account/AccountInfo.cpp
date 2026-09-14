#include <xrpld/rpc/handlers/account/AccountInfo.h>

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
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <rpcspec/Errors.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl::rpc {

namespace {

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
    XRPL_ASSERT(sle.getType() == ltACCOUNT_ROOT, "xrpl::rpc::injectSLE : sle is account root");
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

void
writeAccountRoot(json::Value& value, SLE const& accountRoot, ReadView const& ledger)
{
    // Flags that are always reported.
    static constexpr auto kAccountRootFlags =
        std::to_array<std::pair<std::string_view, LedgerSpecificFlags>>({
            {"allowTrustLineClawback", lsfAllowTrustLineClawback},
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
            {"requireDestinationTag", lsfRequireDestTag},
        });

    // Flags that are only reported when their amendment is enabled. This can't be `constexpr`,
    // since the amendment IDs are computed at runtime.
    static auto const kAmendmentGatedFlags =
        std::to_array<std::tuple<std::string_view, LedgerSpecificFlags, UInt256 const&>>(
            {{"allowTrustLineLocking", lsfAllowTrustLineLocking, featureTokenEscrow}});

    // Every `AccountRoot` flag must be reported by `account_info`, so if a new flag is added, it
    // needs to be added to one of the arrays above. This can't be a `static_assert` because
    // `getAccountRootFlags()` builds its map at runtime.
    XRPL_ASSERT_PARTS(
        kAccountRootFlags.size() + kAmendmentGatedFlags.size() == getAccountRootFlags().size(),
        "xrpl::rpc::writeAccountRoot",
        "number of account flags");

    json::Value jvAccepted(json::ValueType::Object);
    injectSLE(jvAccepted, accountRoot);
    value[jss::account_data] = jvAccepted;

    json::Value acctFlags{json::ValueType::Object};
    for (auto const& [name, flag] : kAccountRootFlags)
        acctFlags[name.data()] = accountRoot.isFlag(flag);

    for (auto const& [name, flag, amendment] : kAmendmentGatedFlags)
    {
        if (ledger.rules().enabled(amendment))
            acctFlags[name.data()] = accountRoot.isFlag(flag);
    }

    value[jss::account_flags] = std::move(acctFlags);

    auto const pseudoFields = getPseudoAccountFields();
    for (auto const& pseudoField : pseudoFields)
    {
        if (accountRoot.isFieldPresent(*pseudoField))
        {
            std::string name = pseudoField->fieldName;
            if (name.ends_with("ID"))
            {
                // Remove the ID suffix from the field name.
                name = name.substr(0, name.size() - 2);
                XRPL_ASSERT_PARTS(
                    !name.empty(), "xrpl::rpc::writeAccountRoot", "name is not empty");
            }
            // ValidPseudoAccounts invariant guarantees that only one field
            // can be set
            value[jss::pseudo_account][jss::type] = name;
            break;
        }
    }
}

[[nodiscard]] json::Value
queueDataJson(std::vector<TxQ::TxDetails> const& txs)
{
    json::Value jvQueueData = json::ValueType::Object;

    if (txs.empty())
    {
        jvQueueData[jss::txn_count] = 0u;
        return jvQueueData;
    }

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
                prevSeqProxy < tx.seqProxy, "xrpl::rpc::queueDataJson : first sorted proxy");
            prevSeqProxy = tx.seqProxy;
            jvTx[jss::seq] = tx.seqProxy.value();
            ++seqCount;
            if (!lowestSeq.has_value())
                lowestSeq = tx.seqProxy.value();
            highestSeq = tx.seqProxy.value();
        }
        else
        {
            XRPL_ASSERT(
                prevSeqProxy < tx.seqProxy, "xrpl::rpc::queueDataJson : second sorted proxy");
            prevSeqProxy = tx.seqProxy;
            jvTx[jss::ticket] = tx.seqProxy.value();
            ++ticketCount;
            if (!lowestTicket.has_value())
                lowestTicket = tx.seqProxy.value();
            highestTicket = tx.seqProxy.value();
        }

        jvTx[jss::fee_level] = to_string(tx.feeLevel);
        if (tx.lastValid.has_value())
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
    if (lowestSeq.has_value())
        jvQueueData[jss::lowest_sequence] = *lowestSeq;
    if (highestSeq.has_value())
        jvQueueData[jss::highest_sequence] = *highestSeq;
    if (lowestTicket.has_value())
        jvQueueData[jss::lowest_ticket] = *lowestTicket;
    if (highestTicket.has_value())
        jvQueueData[jss::highest_ticket] = *highestTicket;

    jvQueueData[jss::auth_change_queued] = anyAuthChanged;
    jvQueueData[jss::max_spend_drops_total] = to_string(totalSpend);

    return jvQueueData;
}

}  // namespace

AccountInfoHandler::AccountInfoHandler(JsonContext& context) : context_(context)
{
}

std::expected<AccountInfoHandler::Output, ::rpc::Status>
AccountInfoHandler::process(Input const& input) const
{
    auto& context = context_.get();

    Output output;
    if (auto const status = getLedger(output.ledger, input.ledger, context);
        status != ::rpc::Status::kOK)
        return std::unexpected{status};
    auto const& ledger = *output.ledger;

    if (!input.account.has_value())
    {
        output.error = RpcActMalformed;
        return output;
    }
    output.account = *input.account;

    auto const accountRoot = ledger.read(keylet::account(output.account));
    if (!accountRoot)
    {
        output.error = RpcActNotFound;
        return output;
    }

    // It doesn't make sense to request the queue
    // with any closed or validated ledger.
    if (input.queue && !ledger.open())
    {
        output.error = RpcInvalidParams;
        return output;
    }

    output.accountRoot = accountRoot;

    // The document[https://xrpl.org/account_info.html#account_info] states
    // that signer_lists is a bool, however assigning any string value
    // works. Do not allow this. This check is for api Version 2 onwards
    // only
    auto const& params = context.params;
    if (context.apiVersion > 1u && params.isMember(jss::signer_lists) &&
        !params[jss::signer_lists].isBool())
    {
        output.error = RpcInvalidParams;
        return output;
    }

    // This code will need to be revisited if in the future we support
    // multiple SignerLists on one account.
    if (input.signerLists)
        output.signerList = ledger.read(keylet::signerList(output.account));

    if (input.queue)
        output.queue = context.app.getTxQ().getAccountTxs(output.account);

    return output;
}

void
AccountInfoHandler::writeResult(json::Value& value, Output const& output) const
{
    auto const& context = context_.get();
    injectLedgerFields(*output.ledger, context, value);

    if (output.accountRoot)
        writeAccountRoot(value, *output.accountRoot, *output.ledger);

    if (output.error.has_value())
    {
        if (*output.error == RpcActNotFound)
            value[jss::account] = toBase58(output.account);
        injectError(*output.error, value);
        return;
    }

    if (output.signerList.has_value())
    {
        // We put the SignerList in an array because of an anticipated
        // future when we support multiple signer lists on one account.
        json::Value jvSignerList = json::ValueType::Array;
        if (*output.signerList)
            jvSignerList.append((*output.signerList)->getJson(JsonOptions::Values::None));

        // Documentation states this is returned as part of the account_info
        // response, but previously the code put it under account_data. We
        // can move this to the documented location from apiVersion 2
        // onwards.
        if (context.apiVersion == 1)
        {
            value[jss::account_data][jss::signer_lists] = std::move(jvSignerList);
        }
        else
        {
            value[jss::signer_lists] = std::move(jvSignerList);
        }
    }

    if (output.queue.has_value())
        value[jss::queue_data] = queueDataJson(*output.queue);
}

}  // namespace xrpl::rpc
