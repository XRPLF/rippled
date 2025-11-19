#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/paths/TrustLine.h>
#include <xrpld/app/rdb/RelationalDatabase.h>
#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/DeliveredAmount.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/ledger/View.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/nftPageMask.h>
#include <xrpl/resource/Fees.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace ripple {
namespace RPC {

namespace {

bool
isValidatedOld(LedgerMaster& ledgerMaster, bool standalone)
{
    if (standalone)
        return false;

    return ledgerMaster.getValidatedLedgerAge() > Tuning::maxValidatedLedgerAge;
}

template <class T>
Status
ledgerFromRequest(T& ledger, JsonContext& context)
{
    ledger.reset();

    auto& params = context.params;

    auto indexValue = params[jss::ledger_index];
    auto hashValue = params[jss::ledger_hash];

    // We need to support the legacy "ledger" field.
    auto& legacyLedger = params[jss::ledger];
    if (legacyLedger)
    {
        if (legacyLedger.asString().size() > 12)
            hashValue = legacyLedger;
        else
            indexValue = legacyLedger;
    }

    if (!hashValue.isNull())
    {
        if (!hashValue.isString())
            return {rpcINVALID_PARAMS, "ledgerHashNotString"};

        uint256 ledgerHash;
        if (!ledgerHash.parseHex(hashValue.asString()))
            return {rpcINVALID_PARAMS, "ledgerHashMalformed"};
        return getLedger(ledger, ledgerHash, context);
    }

    if (!indexValue.isConvertibleTo(Json::stringValue))
        return {rpcINVALID_PARAMS, "ledgerIndexMalformed"};

    auto const index = indexValue.asString();

    if (index == "current" || index.empty())
        return getLedger(ledger, LedgerShortcut::CURRENT, context);

    if (index == "validated")
        return getLedger(ledger, LedgerShortcut::VALIDATED, context);

    if (index == "closed")
        return getLedger(ledger, LedgerShortcut::CLOSED, context);

    std::uint32_t val;
    if (!beast::lexicalCastChecked(val, index))
        return {rpcINVALID_PARAMS, "ledgerIndexMalformed"};

    return getLedger(ledger, val, context);
}
}  // namespace

template <class T, class R>
Status
ledgerFromRequest(T& ledger, GRPCContext<R>& context)
{
    R& request = context.params;
    return ledgerFromSpecifier(ledger, request.ledger(), context);
}

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest>&);

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerDataRequest>&);

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerRequest>&);

template <class T>
Status
ledgerFromSpecifier(
    T& ledger,
    org::xrpl::rpc::v1::LedgerSpecifier const& specifier,
    Context& context)
{
    ledger.reset();

    using LedgerCase = org::xrpl::rpc::v1::LedgerSpecifier::LedgerCase;
    LedgerCase ledgerCase = specifier.ledger_case();
    switch (ledgerCase)
    {
        case LedgerCase::kHash: {
            if (auto hash = uint256::fromVoidChecked(specifier.hash()))
            {
                return getLedger(ledger, *hash, context);
            }
            return {rpcINVALID_PARAMS, "ledgerHashMalformed"};
        }
        case LedgerCase::kSequence:
            return getLedger(ledger, specifier.sequence(), context);
        case LedgerCase::kShortcut:
            [[fallthrough]];
        case LedgerCase::LEDGER_NOT_SET: {
            auto const shortcut = specifier.shortcut();
            if (shortcut ==
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED)
            {
                return getLedger(ledger, LedgerShortcut::VALIDATED, context);
            }
            else
            {
                if (shortcut ==
                        org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT ||
                    shortcut ==
                        org::xrpl::rpc::v1::LedgerSpecifier::
                            SHORTCUT_UNSPECIFIED)
                {
                    return getLedger(ledger, LedgerShortcut::CURRENT, context);
                }
                else if (
                    shortcut ==
                    org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED)
                {
                    return getLedger(ledger, LedgerShortcut::CLOSED, context);
                }
            }
        }
    }

    return Status::OK;
}

template <class T>
Status
getLedger(T& ledger, uint256 const& ledgerHash, Context& context)
{
    ledger = context.ledgerMaster.getLedgerByHash(ledgerHash);
    if (ledger == nullptr)
        return {rpcLGR_NOT_FOUND, "ledgerNotFound"};
    return Status::OK;
}

template <class T>
Status
getLedger(T& ledger, uint32_t ledgerIndex, Context& context)
{
    ledger = context.ledgerMaster.getLedgerBySeq(ledgerIndex);
    if (ledger == nullptr)
    {
        auto cur = context.ledgerMaster.getCurrentLedger();
        if (cur->info().seq == ledgerIndex)
        {
            ledger = cur;
        }
    }

    if (ledger == nullptr)
        return {rpcLGR_NOT_FOUND, "ledgerNotFound"};

    if (ledger->info().seq > context.ledgerMaster.getValidLedgerIndex() &&
        isValidatedOld(context.ledgerMaster, context.app.config().standalone()))
    {
        ledger.reset();
        if (context.apiVersion == 1)
            return {rpcNO_NETWORK, "InsufficientNetworkMode"};
        return {rpcNOT_SYNCED, "notSynced"};
    }

    return Status::OK;
}

template <class T>
Status
getLedger(T& ledger, LedgerShortcut shortcut, Context& context)
{
    if (isValidatedOld(context.ledgerMaster, context.app.config().standalone()))
    {
        if (context.apiVersion == 1)
            return {rpcNO_NETWORK, "InsufficientNetworkMode"};
        return {rpcNOT_SYNCED, "notSynced"};
    }

    if (shortcut == LedgerShortcut::VALIDATED)
    {
        ledger = context.ledgerMaster.getValidatedLedger();
        if (ledger == nullptr)
        {
            if (context.apiVersion == 1)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            return {rpcNOT_SYNCED, "notSynced"};
        }

        XRPL_ASSERT(
            !ledger->open(), "ripple::RPC::getLedger : validated is not open");
    }
    else
    {
        if (shortcut == LedgerShortcut::CURRENT)
        {
            ledger = context.ledgerMaster.getCurrentLedger();
            XRPL_ASSERT(
                ledger->open(), "ripple::RPC::getLedger : current is open");
        }
        else if (shortcut == LedgerShortcut::CLOSED)
        {
            ledger = context.ledgerMaster.getClosedLedger();
            XRPL_ASSERT(
                !ledger->open(), "ripple::RPC::getLedger : closed is not open");
        }
        else
        {
            return {rpcINVALID_PARAMS, "ledgerIndexMalformed"};
        }

        if (ledger == nullptr)
        {
            if (context.apiVersion == 1)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            return {rpcNOT_SYNCED, "notSynced"};
        }

        static auto const minSequenceGap = 10;

        if (ledger->info().seq + minSequenceGap <
            context.ledgerMaster.getValidLedgerIndex())
        {
            ledger.reset();
            if (context.apiVersion == 1)
                return {rpcNO_NETWORK, "InsufficientNetworkMode"};
            return {rpcNOT_SYNCED, "notSynced"};
        }
    }
    return Status::OK;
}

// Explicit instantiation of above three functions
template Status
getLedger<>(std::shared_ptr<ReadView const>&, uint32_t, Context&);

template Status
getLedger<>(
    std::shared_ptr<ReadView const>&,
    LedgerShortcut shortcut,
    Context&);

template Status
getLedger<>(std::shared_ptr<ReadView const>&, uint256 const&, Context&);

// The previous version of the lookupLedger command would accept the
// "ledger_index" argument as a string and silently treat it as a request to
// return the current ledger which, while not strictly wrong, could cause a lot
// of confusion.
//
// The code now robustly validates the input and ensures that the only possible
// values for the "ledger_index" parameter are the index of a ledger passed as
// an integer or one of the strings "current", "closed" or "validated".
// Additionally, the code ensures that the value passed in "ledger_hash" is a
// string and a valid hash. Invalid values will return an appropriate error
// code.
//
// In the absence of the "ledger_hash" or "ledger_index" parameters, the code
// assumes that "ledger_index" has the value "current".
//
// Returns a Json::objectValue.  If there was an error, it will be in that
// return value.  Otherwise, the object contains the field "validated" and
// optionally the fields "ledger_hash", "ledger_index" and
// "ledger_current_index", if they are defined.
Status
lookupLedger(
    std::shared_ptr<ReadView const>& ledger,
    JsonContext& context,
    Json::Value& result)
{
    if (auto status = ledgerFromRequest(ledger, context))
        return status;

    auto& info = ledger->info();

    if (!ledger->open())
    {
        result[jss::ledger_hash] = to_string(info.hash);
        result[jss::ledger_index] = info.seq;
    }
    else
    {
        result[jss::ledger_current_index] = info.seq;
    }

    result[jss::validated] = context.ledgerMaster.isValidated(*ledger);
    return Status::OK;
}

Json::Value
lookupLedger(std::shared_ptr<ReadView const>& ledger, JsonContext& context)
{
    Json::Value result;
    if (auto status = lookupLedger(ledger, context, result))
        status.inject(result);

    return result;
}

std::variant<std::shared_ptr<Ledger const>, Json::Value>
getLedgerByContext(RPC::JsonContext& context)
{
    auto const hasHash = context.params.isMember(jss::ledger_hash);
    auto const hasIndex = context.params.isMember(jss::ledger_index);
    std::uint32_t ledgerIndex = 0;

    auto& ledgerMaster = context.app.getLedgerMaster();
    LedgerHash ledgerHash;

    if ((hasHash && hasIndex) || !(hasHash || hasIndex))
    {
        return RPC::make_param_error(
            "Exactly one of ledger_hash and ledger_index can be set.");
    }

    context.loadType = Resource::feeHeavyBurdenRPC;

    if (hasHash)
    {
        auto const& jsonHash = context.params[jss::ledger_hash];
        if (!jsonHash.isString() || !ledgerHash.parseHex(jsonHash.asString()))
            return RPC::invalid_field_error(jss::ledger_hash);
    }
    else
    {
        auto const& jsonIndex = context.params[jss::ledger_index];
        if (!jsonIndex.isInt())
            return RPC::invalid_field_error(jss::ledger_index);

        // We need a validated ledger to get the hash from the sequence
        if (ledgerMaster.getValidatedLedgerAge() >
            RPC::Tuning::maxValidatedLedgerAge)
        {
            if (context.apiVersion == 1)
                return rpcError(rpcNO_CURRENT);
            return rpcError(rpcNOT_SYNCED);
        }

        ledgerIndex = jsonIndex.asInt();
        auto ledger = ledgerMaster.getValidatedLedger();

        if (ledgerIndex >= ledger->info().seq)
            return RPC::make_param_error("Ledger index too large");
        if (ledgerIndex <= 0)
            return RPC::make_param_error("Ledger index too small");

        auto const j = context.app.journal("RPCHandler");
        // Try to get the hash of the desired ledger from the validated
        // ledger
        auto neededHash = hashOfSeq(*ledger, ledgerIndex, j);
        if (!neededHash)
        {
            // Find a ledger more likely to have the hash of the desired
            // ledger
            auto const refIndex = getCandidateLedger(ledgerIndex);
            auto refHash = hashOfSeq(*ledger, refIndex, j);
            XRPL_ASSERT(
                refHash,
                "ripple::RPC::getLedgerByContext : nonzero ledger hash");

            ledger = ledgerMaster.getLedgerByHash(*refHash);
            if (!ledger)
            {
                // We don't have the ledger we need to figure out which
                // ledger they want. Try to get it.

                if (auto il = context.app.getInboundLedgers().acquire(
                        *refHash, refIndex, InboundLedger::Reason::GENERIC))
                {
                    Json::Value jvResult = RPC::make_error(
                        rpcLGR_NOT_FOUND,
                        "acquiring ledger containing requested index");
                    jvResult[jss::acquiring] =
                        getJson(LedgerFill(*il, &context));
                    return jvResult;
                }

                if (auto il = context.app.getInboundLedgers().find(*refHash))
                {
                    Json::Value jvResult = RPC::make_error(
                        rpcLGR_NOT_FOUND,
                        "acquiring ledger containing requested index");
                    jvResult[jss::acquiring] = il->getJson(0);
                    return jvResult;
                }

                // Likely the app is shutting down
                return Json::Value();
            }

            neededHash = hashOfSeq(*ledger, ledgerIndex, j);
        }
        XRPL_ASSERT(
            neededHash,
            "ripple::RPC::getLedgerByContext : nonzero needed hash");
        ledgerHash = neededHash ? *neededHash : beast::zero;  // kludge
    }

    // Try to get the desired ledger
    // Verify all nodes even if we think we have it
    auto ledger = context.app.getInboundLedgers().acquire(
        ledgerHash, ledgerIndex, InboundLedger::Reason::GENERIC);

    // In standalone mode, accept the ledger from the ledger cache
    if (!ledger && context.app.config().standalone())
        ledger = ledgerMaster.getLedgerByHash(ledgerHash);

    if (ledger)
        return ledger;

    if (auto il = context.app.getInboundLedgers().find(ledgerHash))
        return il->getJson(0);

    return RPC::make_error(
        rpcNOT_READY, "findCreate failed to return an inbound ledger");
}

}  // namespace RPC
}  // namespace ripple
