#include <xrpld/rpc/detail/RPCLedgerHelpers.h>

#include <xrpld/app/ledger/InboundLedger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerToJson.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerShortcut.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/jss.h>

#include <org/xrpl/rpc/v1/ledger.pb.h>

#include <cstdint>
#include <expected>
#include <memory>

namespace xrpl::RPC {

namespace {

bool
isValidatedOld(LedgerMaster& ledgerMaster, bool standalone)
{
    if (standalone)
        return false;

    return ledgerMaster.getValidatedLedgerAge() > Tuning::kMaxValidatedLedgerAge;
}

template <class T>
Status
ledgerFromHash(
    T& ledger,
    json::Value hash,
    Context const& context,
    json::StaticString const fieldName)
{
    uint256 ledgerHash;
    if (!ledgerHash.parseHex(hash.asString()))
        return {RpcInvalidParams, expectedFieldMessage(fieldName, "hex string")};
    return getLedger(ledger, ledgerHash, context);
}

template <class T>
Status
ledgerFromIndex(
    T& ledger,
    json::Value indexValue,
    Context const& context,
    json::StaticString const fieldName)
{
    auto const index = indexValue.asString();

    if (index == "current" || index.empty())
        return getLedger(ledger, LedgerShortcut::Current, context);

    if (index == "validated")
        return getLedger(ledger, LedgerShortcut::Validated, context);

    if (index == "closed")
        return getLedger(ledger, LedgerShortcut::Closed, context);

    std::uint32_t iVal = 0;
    if (!beast::lexicalCastChecked(iVal, index))
        return {RpcInvalidParams, expectedFieldMessage(fieldName, "string or number")};

    return getLedger(ledger, iVal, context);
}

template <class T>
Status
ledgerFromRequest(T& ledger, JsonContext const& context)
{
    ledger.reset();

    auto& params = context.params;
    auto const hasLedger = context.params.isMember(jss::ledger);
    auto const hasHash = context.params.isMember(jss::ledger_hash);
    auto const hasIndex = context.params.isMember(jss::ledger_index);

    if ((hasLedger + hasHash + hasIndex) > 1)
    {
        // while `ledger` is still supported, it is deprecated
        // and therefore shouldn't be mentioned in the error message
        if (hasLedger)
        {
            return {
                RpcInvalidParams,
                "Exactly one of 'ledger', 'ledger_hash', or "
                "'ledger_index' can be specified."};
        }
        return {
            RpcInvalidParams,
            "Exactly one of 'ledger_hash' or "
            "'ledger_index' can be specified."};
    }

    // We need to support the legacy "ledger" field.
    if (hasLedger)
    {
        auto& legacyLedger = params[jss::ledger];
        if (!legacyLedger.isString() && !legacyLedger.isUInt() && !legacyLedger.isInt())
        {
            return {RpcInvalidParams, expectedFieldMessage(jss::ledger, "string or number")};
        }
        if (legacyLedger.isString() && legacyLedger.asString().size() == 64)
        {
            return ledgerFromHash(ledger, legacyLedger, context, jss::ledger);
        }

        return ledgerFromIndex(ledger, legacyLedger, context, jss::ledger);
    }

    if (hasHash)
    {
        auto const& ledgerHash = params[jss::ledger_hash];
        if (!ledgerHash.isString())
            return {RpcInvalidParams, expectedFieldMessage(jss::ledger_hash, "hex string")};
        return ledgerFromHash(ledger, ledgerHash, context, jss::ledger_hash);
    }

    if (hasIndex)
    {
        auto const& ledgerIndex = params[jss::ledger_index];
        if (!ledgerIndex.isString() && !ledgerIndex.isUInt() && !ledgerIndex.isInt())
        {
            return {RpcInvalidParams, expectedFieldMessage(jss::ledger_index, "string or number")};
        }
        return ledgerFromIndex(ledger, ledgerIndex, context, jss::ledger_index);
    }

    // nothing specified, `index` has a default setting
    return getLedger(ledger, LedgerShortcut::Current, context);
}
}  // namespace

template <class T, class R>
Status
ledgerFromRequest(T& ledger, GRPCContext<R> const& context)
{
    R const& request = context.params;
    return ledgerFromSpecifier(ledger, request.ledger(), context);
}

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerEntryRequest> const&);

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerDataRequest> const&);

// explicit instantiation of above function
template Status
ledgerFromRequest<>(
    std::shared_ptr<ReadView const>&,
    GRPCContext<org::xrpl::rpc::v1::GetLedgerRequest> const&);

template <class T>
Status
ledgerFromSpecifier(
    T& ledger,
    org::xrpl::rpc::v1::LedgerSpecifier const& specifier,
    Context const& context)
{
    ledger.reset();

    using LedgerCase = org::xrpl::rpc::v1::LedgerSpecifier::LedgerCase;
    LedgerCase const ledgerCase = specifier.ledger_case();
    switch (ledgerCase)
    {
        case LedgerCase::kHash: {
            if (auto hash = uint256::fromVoidChecked(specifier.hash()))
            {
                return getLedger(ledger, *hash, context);
            }
            return {RpcInvalidParams, "ledgerHashMalformed"};
        }
        case LedgerCase::kSequence:
            return getLedger(ledger, specifier.sequence(), context);
        case LedgerCase::kShortcut:
            [[fallthrough]];
        case LedgerCase::LEDGER_NOT_SET: {
            auto const shortcut = specifier.shortcut();
            if (shortcut == org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED)
            {
                return getLedger(ledger, LedgerShortcut::Validated, context);
            }

            if (shortcut == org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT ||
                shortcut == org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_UNSPECIFIED)
            {
                return getLedger(ledger, LedgerShortcut::Current, context);
            }
            if (shortcut == org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED)
            {
                return getLedger(ledger, LedgerShortcut::Closed, context);
            }
        }
    }

    return Status::kOK;
}

template <class T>
Status
getLedger(T& ledger, uint256 const& ledgerHash, Context const& context)
{
    ledger = context.ledgerMaster.getLedgerByHash(ledgerHash);
    if (ledger == nullptr)
        return {RpcLgrNotFound, "ledgerNotFound"};
    return Status::kOK;
}

template <class T>
Status
getLedger(T& ledger, uint32_t ledgerIndex, Context const& context)
{
    ledger = context.ledgerMaster.getLedgerBySeq(ledgerIndex);
    if (ledger == nullptr)
    {
        auto cur = context.ledgerMaster.getCurrentLedger();
        if (cur->header().seq == ledgerIndex)
        {
            ledger = cur;
        }
    }

    if (ledger == nullptr)
        return {RpcLgrNotFound, "ledgerNotFound"};

    if (ledger->header().seq > context.ledgerMaster.getValidLedgerIndex() &&
        isValidatedOld(context.ledgerMaster, context.app.config().standalone()))
    {
        ledger.reset();
        if (context.apiVersion == 1)
            return {RpcNoNetwork, "InsufficientNetworkMode"};
        return {RpcNotSynced, "notSynced"};
    }

    return Status::kOK;
}

template <class T>
Status
getLedger(T& ledger, LedgerShortcut shortcut, Context const& context)
{
    if (isValidatedOld(context.ledgerMaster, context.app.config().standalone()))
    {
        if (context.apiVersion == 1)
            return {RpcNoNetwork, "InsufficientNetworkMode"};
        return {RpcNotSynced, "notSynced"};
    }

    if (shortcut == LedgerShortcut::Validated)
    {
        ledger = context.ledgerMaster.getValidatedLedger();
        if (ledger == nullptr)
        {
            if (context.apiVersion == 1)
                return {RpcNoNetwork, "InsufficientNetworkMode"};
            return {RpcNotSynced, "notSynced"};
        }

        XRPL_ASSERT(!ledger->open(), "xrpl::RPC::getLedger : validated is not open");
    }
    else
    {
        if (shortcut == LedgerShortcut::Current)
        {
            ledger = context.ledgerMaster.getCurrentLedger();
            XRPL_ASSERT(ledger->open(), "xrpl::RPC::getLedger : current is open");
        }
        else if (shortcut == LedgerShortcut::Closed)
        {
            ledger = context.ledgerMaster.getClosedLedger();
            XRPL_ASSERT(!ledger->open(), "xrpl::RPC::getLedger : closed is not open");
        }
        else
        {
            return {RpcInvalidParams, "ledgerIndexMalformed"};
        }

        if (ledger == nullptr)
        {
            if (context.apiVersion == 1)
                return {RpcNoNetwork, "InsufficientNetworkMode"};
            return {RpcNotSynced, "notSynced"};
        }

        static auto const kMinSequenceGap = 10;

        if (ledger->header().seq + kMinSequenceGap < context.ledgerMaster.getValidLedgerIndex())
        {
            ledger.reset();
            if (context.apiVersion == 1)
                return {RpcNoNetwork, "InsufficientNetworkMode"};
            return {RpcNotSynced, "notSynced"};
        }
    }
    return Status::kOK;
}

// Explicit instantiation of above three functions
template Status
getLedger<>(std::shared_ptr<ReadView const>&, uint32_t, Context const&);

template Status
getLedger<>(std::shared_ptr<ReadView const>&, LedgerShortcut shortcut, Context const&);

template Status
getLedger<>(std::shared_ptr<ReadView const>&, uint256 const&, Context const&);

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
// Returns a json::ValueType::Object.  If there was an error, it will be in that
// return value.  Otherwise, the object contains the field "validated" and
// optionally the fields "ledger_hash", "ledger_index" and
// "ledger_current_index", if they are defined.
Status
lookupLedger(
    std::shared_ptr<ReadView const>& ledger,
    JsonContext const& context,
    json::Value& result)
{
    if (auto status = ledgerFromRequest(ledger, context))
        return status;

    auto& info = ledger->header();

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
    return Status::kOK;
}

json::Value
lookupLedger(std::shared_ptr<ReadView const>& ledger, JsonContext const& context)
{
    json::Value result;
    if (auto status = lookupLedger(ledger, context, result))
        status.inject(result);

    return result;
}

std::expected<std::shared_ptr<Ledger const>, json::Value>
getOrAcquireLedger(RPC::JsonContext const& context)
{
    auto const hasHash = context.params.isMember(jss::ledger_hash);
    auto const hasIndex = context.params.isMember(jss::ledger_index);
    std::uint32_t ledgerIndex = 0;

    auto& ledgerMaster = context.app.getLedgerMaster();
    LedgerHash ledgerHash;

    if ((static_cast<int>(hasHash) + static_cast<int>(hasIndex)) != 1)
    {
        return std::unexpected(
            RPC::makeParamError(
                "Exactly one of 'ledger_hash' or "
                "'ledger_index' can be specified."));
    }

    if (hasHash)
    {
        auto const& jsonHash = context.params.get(jss::ledger_hash, json::ValueType::Null);
        if (!jsonHash.isString() || !ledgerHash.parseHex(jsonHash.asString()))
            return std::unexpected(RPC::expectedFieldError(jss::ledger_hash, "hex string"));
    }
    else
    {
        auto const& jsonIndex = context.params.get(jss::ledger_index, json::ValueType::Null);
        if (!jsonIndex.isInt() && !jsonIndex.isUInt())
            return std::unexpected(RPC::expectedFieldError(jss::ledger_index, "number"));

        // We need a validated ledger to get the hash from the sequence
        if (ledgerMaster.getValidatedLedgerAge() > RPC::Tuning::kMaxValidatedLedgerAge)
        {
            if (context.apiVersion == 1)
                return std::unexpected(rpcError(RpcNoCurrent));
            return std::unexpected(rpcError(RpcNotSynced));
        }

        ledgerIndex = jsonIndex.asInt();
        auto ledger = ledgerMaster.getValidatedLedger();

        if (ledgerIndex >= ledger->header().seq)
            return std::unexpected(RPC::makeParamError("Ledger index too large"));
        if (ledgerIndex <= 0)
            return std::unexpected(RPC::makeParamError("Ledger index too small"));

        auto const j = context.app.getJournal("RPCHandler");
        // Try to get the hash of the desired ledger from the validated
        // ledger
        auto neededHash = hashOfSeq(*ledger, ledgerIndex, j);
        if (!neededHash)
        {
            // Find a ledger more likely to have the hash of the desired
            // ledger
            auto const refIndex = getCandidateLedger(ledgerIndex);
            auto refHash = hashOfSeq(*ledger, refIndex, j);
            XRPL_ASSERT(refHash, "xrpl::RPC::getOrAcquireLedger : nonzero ledger hash");

            // NOLINTBEGIN(bugprone-unchecked-optional-access) assert above
            ledger = ledgerMaster.getLedgerByHash(*refHash);
            if (!ledger)
            {
                // We don't have the ledger we need to figure out which
                // ledger they want. Try to get it.

                if (auto il = context.app.getInboundLedgers().acquire(
                        *refHash, refIndex, InboundLedger::Reason::GENERIC))
                {
                    json::Value jvResult = RPC::makeError(
                        RpcLgrNotFound, "acquiring ledger containing requested index");
                    jvResult[jss::acquiring] = getJson(LedgerFill(*il, &context));
                    return std::unexpected(jvResult);
                }

                if (auto il = context.app.getInboundLedgers().find(*refHash))
                // NOLINTEND(bugprone-unchecked-optional-access)
                {
                    json::Value jvResult = RPC::makeError(
                        RpcLgrNotFound, "acquiring ledger containing requested index");
                    jvResult[jss::acquiring] = il->getJson(0);
                    return std::unexpected(jvResult);
                }

                // Likely the app is shutting down
                return std::unexpected(json::Value());
            }

            neededHash = hashOfSeq(*ledger, ledgerIndex, j);
        }
        XRPL_ASSERT(neededHash, "xrpl::RPC::getOrAcquireLedger : nonzero needed hash");
        ledgerHash = neededHash ? *neededHash : beast::kZero;  // kludge
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
        return std::unexpected(il->getJson(0));

    return std::unexpected(
        RPC::makeError(RpcNotReady, "findCreate failed to return an inbound ledger"));
}

}  // namespace xrpl::RPC
