#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Slice.h>
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
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/format/free_funcs.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace xrpl {

/** Serialize a ledger entry to JSON, augmenting account-root entries with a Gravatar URL.
 *
 *  Writes `sle.getJson(JsonOptions::none)` into `jv`.  When `sle` is an
 *  `ltACCOUNT_ROOT` and the `sfEmailHash` field is set, appends a
 *  `urlgravatar` field containing the Gravatar URL for that MD5 hash.  If
 *  `sle` is any other entry type, sets `jv["Invalid"] = true` as a defensive
 *  signal; in practice `doAccountInfo` only passes account-root SLEs, so that
 *  branch is a safety net.
 *
 *  @param jv  Output JSON object; overwritten entirely on each call.
 *  @param sle The ledger entry to serialize.
 *  @note The Gravatar URL is constructed over HTTP, not HTTPS (known technical
 *      debt; see the VFALCO TODO comment in the implementation).
 */
void
injectSLE(json::Value& jv, SLE const& sle)
{
    jv = sle.getJson(JsonOptions::Values::None);
    if (sle.getType() == ltACCOUNT_ROOT)
    {
        if (sle.isFieldPresent(sfEmailHash))
        {
            auto const& hash = sle.getFieldH128(sfEmailHash);
            Blob const b(hash.begin(), hash.end());
            std::string md5 = strHex(makeSlice(b));
            boost::to_lower(md5);
            // VFALCO TODO Give a name to this constant and move it
            //             to a more visible location.
            jv[jss::urlgravatar] = str(boost::format("https://www.gravatar.com/avatar/%s") % md5);
        }
    }
    else
    {
        jv[jss::Invalid] = true;
    }
}

/** Handle the `account_info` RPC command.
 *
 *  Reads the account root directly from the requested ledger and returns its
 *  fields, a named-boolean `account_flags` map, and optional signer-list and
 *  transaction-queue data.
 *
 *  The account identifier is read from the `account` field or, for backward
 *  compatibility, the legacy `ident` alias; both must be strings.  Ledger
 *  resolution runs before account lookup — an unknown ledger hash or index
 *  causes an early return before any account state is touched.
 *
 *  **Flag serialization** — `account_flags` always contains the nine core
 *  flags (`kLS_FLAGS`) and the four "disallow incoming" flags
 *  (`kDISALLOW_INCOMING_FLAGS`).  `allowTrustLineClawback` is added only when
 *  `featureClawback` is active in the requested ledger; `allowTrustLineLocking`
 *  only when `featureTokenEscrow` is active.  This prevents false `false`
 *  readings against older ledgers that have no concept of those flags.
 *
 *  **Signer lists** — when `signer_lists: true` is present the result includes
 *  a single-element JSON array (reserved for a potential future multi-list
 *  protocol).  On API v1 the array is nested inside `account_data`; on API v2+
 *  it moves to the top-level response.  API v2+ also enforces strict boolean
 *  typing: a non-boolean `signer_lists` value returns `rpcINVALID_PARAMS`.
 *
 *  **Queue data** — when `queue: true` is present the handler calls
 *  `TxQ::getAccountTxs` and serializes each pending entry with its sequence or
 *  ticket number, fee level, last-valid ledger, absolute fee, and
 *  `max_spend_drops`.  This path requires the target ledger to be open;
 *  requesting queue state against a closed or validated ledger returns
 *  `rpcINVALID_PARAMS` because the queue is cleared at ledger close.
 *
 *  **Pseudo-accounts** — if the account root carries a pseudo-account designator
 *  field (e.g., `sfAMMID`, `sfVaultID`, `sfLoanBrokerID`), the response includes
 *  `pseudo_account.type` set to the trimmed field name (trailing `"ID"` suffix
 *  removed).  Only one such field can be set per the `ValidPseudoAccounts`
 *  invariant.
 *
 *  @param context  The RPC dispatch envelope carrying parsed parameters,
 *      ledger access, app state, and API version.
 *  @return A JSON object with `account_data`, `account_flags`, and optionally
 *      `signer_lists`, `queue_data`, and `pseudo_account`; or an error object
 *      on any validation failure.
 *
 *  @note Error conditions:
 *      - `account`/`ident` wrong type → `rpcINVALID_FIELD`
 *      - Neither field present → `rpcMISSING_FIELD`
 *      - Unknown ledger → ledger-level error from `lookupLedger`
 *      - Malformed Base58 address → `rpcACT_MALFORMED`
 *      - `queue: true` on a non-open ledger → `rpcINVALID_PARAMS`
 *      - `signer_lists` non-boolean on API v2+ → `rpcINVALID_PARAMS`
 *      - Account not found → `rpcACT_NOT_FOUND`
 */
json::Value
doAccountInfo(RPC::JsonContext& context)
{
    auto& params = context.params;

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
    else
    {
        return RPC::missingFieldError(jss::account);
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

    // Nine core account-root flags always present in `account_flags`, regardless
    // of which amendments are enabled.  Clients can rely on this stable key set.
    static constexpr std::array<std::pair<std::string_view, LedgerSpecificFlags>, 9> kLS_FLAGS{
        {{"defaultRipple", lsfDefaultRipple},
         {"depositAuth", lsfDepositAuth},
         {"disableMasterKey", lsfDisableMaster},
         {"disallowIncomingXRP", lsfDisallowXRP},
         {"globalFreeze", lsfGlobalFreeze},
         {"noFreeze", lsfNoFreeze},
         {"passwordSpent", lsfPasswordSpent},
         {"requireAuthorization", lsfRequireAuth},
         {"requireDestinationTag", lsfRequireDestTag}}};

    // Four "disallow incoming" flags introduced with the NFToken amendments.
    // Always serialized into `account_flags`; the bit is simply `false` until
    // the account sets it, giving clients a consistent key set.
    static constexpr std::array<std::pair<std::string_view, LedgerSpecificFlags>, 4>
        kDISALLOW_INCOMING_FLAGS{
            {{"disallowIncomingNFTokenOffer", lsfDisallowIncomingNFTokenOffer},
             {"disallowIncomingCheck", lsfDisallowIncomingCheck},
             {"disallowIncomingPayChan", lsfDisallowIncomingPayChan},
             {"disallowIncomingTrustline", lsfDisallowIncomingTrustline}}};

    // Amendment-gated flag; emitted in `account_flags` only when `featureClawback`
    // is active in the requested ledger, preventing a misleading `false` against
    // older ledgers that have no concept of the flag.
    static constexpr std::pair<std::string_view, LedgerSpecificFlags>
        kALLOW_TRUST_LINE_CLAWBACK_FLAG{"allowTrustLineClawback", lsfAllowTrustLineClawback};

    // Amendment-gated flag; emitted in `account_flags` only when `featureTokenEscrow`
    // is active in the requested ledger, for the same reason as
    // `kALLOW_TRUST_LINE_CLAWBACK_FLAG`.
    static constexpr std::pair<std::string_view, LedgerSpecificFlags>
        kALLOW_TRUST_LINE_LOCKING_FLAG{"allowTrustLineLocking", lsfAllowTrustLineLocking};

    auto const sleAccepted = ledger->read(keylet::account(accountID));
    if (sleAccepted)
    {
        auto const queue = params.isMember(jss::queue) && params[jss::queue].asBool();

        if (queue && !ledger->open())
        {
            RPC::injectError(RpcInvalidParams, result);
            return result;
        }

        json::Value jvAccepted(json::ValueType::Object);
        injectSLE(jvAccepted, *sleAccepted);
        result[jss::account_data] = jvAccepted;

        json::Value acctFlags{json::ValueType::Object};
        for (auto const& lsf : kLS_FLAGS)
            acctFlags[lsf.first.data()] = sleAccepted->isFlag(lsf.second);

        for (auto const& lsf : kDISALLOW_INCOMING_FLAGS)
            acctFlags[lsf.first.data()] = sleAccepted->isFlag(lsf.second);

        if (ledger->rules().enabled(featureClawback))
        {
            acctFlags[kALLOW_TRUST_LINE_CLAWBACK_FLAG.first.data()] =
                sleAccepted->isFlag(kALLOW_TRUST_LINE_CLAWBACK_FLAG.second);
        }

        if (ledger->rules().enabled(featureTokenEscrow))
        {
            acctFlags[kALLOW_TRUST_LINE_LOCKING_FLAG.first.data()] =
                sleAccepted->isFlag(kALLOW_TRUST_LINE_LOCKING_FLAG.second);
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
                    name = name.substr(0, name.size() - 2);
                    XRPL_ASSERT_PARTS(!name.empty(), "xrpl::doAccountInfo", "name is not empty");
                }
                // ValidPseudoAccounts invariant guarantees that only one field
                // can be set
                result[jss::pseudo_account][jss::type] = name;
                break;
            }
        }

        if (context.apiVersion > 1u && params.isMember(jss::signer_lists) &&
            !params[jss::signer_lists].isBool())
        {
            RPC::injectError(RpcInvalidParams, result);
            return result;
        }

        if (params.isMember(jss::signer_lists) && params[jss::signer_lists].asBool())
        {
            // The array is pre-allocated in anticipation of a future protocol
            // that allows multiple signer lists per account.
            json::Value jvSignerList = json::ValueType::Array;

            auto const sleSigners = ledger->read(keylet::signers(accountID));
            if (sleSigners)
                jvSignerList.append(sleSigners->getJson(JsonOptions::Values::None));

            if (context.apiVersion == 1)
            {
                result[jss::account_data][jss::signer_lists] = std::move(jvSignerList);
            }
            else
            {
                result[jss::signer_lists] = std::move(jvSignerList);
            }
        }
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

                SeqProxy prevSeqProxy = SeqProxy::sequence(0);
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
        RPC::injectError(RpcActNotFound, result);
    }

    return result;
}

}  // namespace xrpl
