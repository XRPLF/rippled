/** @file
 *  Implements the `noripple_check` RPC handler.
 *
 *  Audits an account's No Ripple flag configuration and, on request,
 *  generates ready-to-submit `AccountSet` and `TrustSet` transaction
 *  templates that correct each detected misconfiguration.
 */
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpld/rpc/detail/RPCLedgerHelpers.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/LoadFeeTrack.h>

#include <cstdint>
#include <memory>

namespace xrpl {

/** Populate common fields for a remediation transaction template.
 *
 *  Writes `Sequence`, `Account`, and `Fee` into @p txArray.  The sequence
 *  number is taken from @p sequence and then incremented so that consecutive
 *  calls produce monotonically increasing sequence numbers within a single
 *  response.  The fee is the reference transaction cost scaled to the current
 *  network load via `scaleFeeLoad`, giving the client a load-calibrated
 *  estimate rather than a static value.
 *
 *  @param context  The RPC call context (provides app handle for fee track).
 *  @param txArray  The JSON object to populate with the three common fields.
 *  @param accountID The account that will submit the transaction.
 *  @param sequence  In/out: current sequence number; incremented after use.
 *  @param ledger   The ledger whose fee schedule is used.
 */
static void
fillTransaction(
    RPC::JsonContext& context,
    json::Value& txArray,
    AccountID const& accountID,
    std::uint32_t& sequence,
    ReadView const& ledger)
{
    txArray["Sequence"] = json::UInt(sequence++);
    txArray["Account"] = toBase58(accountID);
    auto& fees = ledger.fees();
    txArray["Fee"] = scaleFeeLoad(fees.base, context.app.getFeeTrack(), fees, false).jsonClipped();
}

/** Implement the `noripple_check` RPC command.
 *
 *  Identifies No Ripple flag misconfigurations on an account's trust lines
 *  relative to the declared role:
 *  - **gateway**: `lsfDefaultRipple` must be set on the account; No Ripple
 *    must be cleared on every trust line so payments can ripple through.
 *  - **user**: `lsfDefaultRipple` must be clear; No Ripple must be set on
 *    every trust line to prevent unintended third-party routing.
 *
 *  When `transactions` is `true`, the response also contains an ordered
 *  array of unsigned `AccountSet` and/or `TrustSet` transaction templates
 *  that, if signed and submitted in order, will resolve every reported
 *  problem.  Sequence numbers in the templates are contiguous starting from
 *  the account's current ledger sequence, and fees are load-scaled at
 *  request time.
 *
 *  The `limit` parameter (range 10–400, default 300) caps the number of
 *  trust lines inspected, not just the number of problems reported.
 *  Inspection stops after `limit` `ltRIPPLE_STATE` objects are processed,
 *  regardless of how many problems were found.
 *
 *  @param context  RPC call context containing `params`, app handle, and
 *      API version.
 *  @return A JSON object with a `problems` array (always present) and an
 *      optional `transactions` array.  Returns a JSON error object on any
 *      validation failure, unknown ledger, malformed account string
 *      (`actMalformed`), or missing account (`actNotFound`).
 *  @note Starting with API version 2, the `transactions` field is rejected
 *      unless it is a strict JSON boolean.  Earlier versions silently coerce
 *      any truthy value via `asBool()`.
 *  @note When `transactions` is `false`, the handler still allocates a local
 *      `dummy` Json::Value and routes all `append` calls through it.  This
 *      avoids per-item branching at the cost of discarded work; the
 *      `NOLINT(misc-const-correctness)` annotation on `dummy` acknowledges
 *      that the value is intentionally mutated even though its final state
 *      is unused.
 */
json::Value
doNoRippleCheck(RPC::JsonContext& context)
{
    auto const& params(context.params);
    if (!params.isMember(jss::account))
        return RPC::missingFieldError("account");

    if (!params.isMember("role"))
        return RPC::missingFieldError("role");

    if (!params[jss::account].isString())
        return RPC::invalidFieldError(jss::account);

    bool roleGateway = false;
    {
        std::string const role = params["role"].asString();
        if (role == "gateway")
        {
            roleGateway = true;
        }
        else if (role != "user")
        {
            return RPC::invalidFieldError("role");
        }
    }

    unsigned int limit = 0;
    if (auto err = readLimitField(limit, RPC::Tuning::kNO_RIPPLE_CHECK, context))
        return *err;

    bool transactions = false;
    if (params.isMember(jss::transactions))
        transactions = params["transactions"].asBool();

    // API v2+: reject non-boolean values; v1 silently coerces via asBool().
    if (context.apiVersion > 1u && params.isMember(jss::transactions) &&
        !params[jss::transactions].isBool())
    {
        return RPC::invalidFieldError(jss::transactions);
    }

    std::shared_ptr<ReadView const> ledger;
    auto result = RPC::lookupLedger(ledger, context);
    if (!ledger)
        return result;

    json::Value dummy;  // NOLINT(misc-const-correctness)
    json::Value& jvTransactions =
        transactions ? (result[jss::transactions] = json::ValueType::Array) : dummy;

    auto id = parseBase58<AccountID>(params[jss::account].asString());
    if (!id)
    {
        RPC::injectError(RpcActMalformed, result);
        return result;
    }
    auto const accountID{id.value()};
    auto const sle = ledger->read(keylet::account(accountID));
    if (!sle)
        return rpcError(RpcActNotFound);

    std::uint32_t seq = sle->getFieldU32(sfSequence);

    json::Value& problems = (result["problems"] = json::ValueType::Array);

    bool const bDefaultRipple = (sle->getFieldU32(sfFlags) & lsfDefaultRipple) != 0u;

    if (bDefaultRipple && !roleGateway)
    {
        problems.append(
            "You appear to have set your default ripple flag even though you "
            "are not a gateway. This is not recommended unless you are "
            "experimenting");
    }
    else if (roleGateway && !bDefaultRipple)
    {
        problems.append("You should immediately set your default ripple flag");
        if (transactions)
        {
            json::Value& tx = jvTransactions.append(json::ValueType::Object);
            tx["TransactionType"] = jss::AccountSet;
            tx["SetFlag"] = 8;
            fillTransaction(context, tx, accountID, seq, *ledger);
        }
    }

    forEachItemAfter(
        *ledger, accountID, uint256(), 0, limit, [&](std::shared_ptr<SLE const> const& ownedItem) {
            if (ownedItem->getType() == ltRIPPLE_STATE)
            {
                bool const bLow = accountID == ownedItem->getFieldAmount(sfLowLimit).getIssuer();

                bool const bNoRipple =
                    ownedItem->getFieldU32(sfFlags) & (bLow ? lsfLowNoRipple : lsfHighNoRipple);

                std::string problem;
                bool needFix = false;
                if (bNoRipple && roleGateway)
                {
                    problem = "You should clear the no ripple flag on your ";
                    needFix = true;
                }
                else if (!roleGateway && !bNoRipple)
                {
                    problem = "You should probably set the no ripple flag on your ";
                    needFix = true;
                }
                if (needFix)
                {
                    AccountID const peer =
                        ownedItem->getFieldAmount(bLow ? sfHighLimit : sfLowLimit).getIssuer();
                    STAmount const peerLimit =
                        ownedItem->getFieldAmount(bLow ? sfHighLimit : sfLowLimit);
                    problem += to_string(peerLimit.get<Issue>().currency);
                    problem += " line to ";
                    problem += to_string(peerLimit.getIssuer());
                    problems.append(problem);

                    STAmount limitAmount(
                        ownedItem->getFieldAmount(bLow ? sfLowLimit : sfHighLimit));
                    limitAmount.get<Issue>().account = peer;

                    json::Value& tx = jvTransactions.append(json::ValueType::Object);
                    tx["TransactionType"] = jss::TrustSet;
                    tx["LimitAmount"] = limitAmount.getJson(JsonOptions::Values::None);
                    tx["Flags"] = bNoRipple ? tfClearNoRipple : tfSetNoRipple;
                    fillTransaction(context, tx, accountID, seq, *ledger);

                    return true;
                }
            }
            return false;
        });

    return result;
}

}  // namespace xrpl
